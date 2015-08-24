/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "AESGCMEncryption.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include <assert.h>

const size_t iv_size = 64;
const size_t end_marker_zeros=2;

AESGCMEncryption::AESGCMEncryption( const std::string& key, bool hash_password)
	: encryption(), encryption_filter(encryption), iv_done(false), end_marker_state(0)
{
	if(hash_password)
	{
		m_sbbKey.resize(CryptoPP::SHA256::DIGESTSIZE);
		CryptoPP::SHA256().CalculateDigest(m_sbbKey, (byte*)key.c_str(), key.size() );
	}
	else
	{
		m_sbbKey.resize(key.size());
		memcpy(m_sbbKey.BytePtr(), key.c_str(), key.size());
	}

	m_IV.resize(iv_size);
		
	CryptoPP::AutoSeededRandomPool prng;

	prng.GenerateBlock(m_IV.BytePtr(), m_IV.size());

	encryption.SetKeyWithIV(m_sbbKey.BytePtr(), m_sbbKey.size(),
		m_IV.BytePtr(), m_IV.size());

	iv_done=false;

	assert(encryption.CanUseStructuredIVs());
	assert(encryption.IsResynchronizable());
}


void AESGCMEncryption::put( const char *data, size_t data_size )
{
	encryption_filter.Put(reinterpret_cast<const byte*>(data), data_size);
}

void AESGCMEncryption::flush()
{
	encryption_filter.MessageEnd();
	encryption.Resynchonize();
	end_markers.push_back(encryption_filter.MaxRetrievable());
}

std::string AESGCMEncryption::get()
{
	std::string ret;

	size_t iv_add = iv_done ? 0 : m_IV.size();

	size_t max_retrievable;

	bool add_end_marker=false;
	if(!end_markers.empty())
	{
		max_retrievable = end_markers[0];
		end_markers.erase(end_markers.begin());
		add_end_marker=true;
	}
	else
	{
		max_retrievable = encryption_filter.MaxRetrievable();
	}

	ret.resize(max_retrievable+iv_add + ( add_end_marker ? (end_marker_zeros + 1) : 0 ) );

	if(!iv_done)
	{
		iv_add=m_IV.size();
		memcpy(&ret[0], m_IV.BytePtr(), m_IV.size());
		iv_done=true;
	}

	if(ret.size()>iv_add)
	{
		size_t nb = encryption_filter.Get(reinterpret_cast<byte*>(&ret[iv_add]), max_retrievable);
		assert(nb==max_retrievable);
		/*if(nb!=max_retrievable)
		{
			ret.resize(nb+iv_add+ ( add_end_marker ? (end_marker_zeros + 1) : 0 ));
		}*/
		escapeEndMarker(ret, nb, iv_add);
		decEndMarkers(nb);
	}

	if(add_end_marker)	
	{
		//The rest is already zero
		ret[ret.size()-1]=1;
		end_marker_state=0;
		encryption_filter.GetNextMessage();
	}

	return ret;
}

void AESGCMEncryption::decEndMarkers( size_t n )
{
	for(size_t i=0;i<end_markers.size();++i)
		end_markers[i]-=n;
}

void AESGCMEncryption::escapeEndMarker(std::string& ret, size_t size, size_t offset)
{
	for(size_t i=offset;i<size;)
	{
		char ch=ret[i];

		if(end_marker_state==0 && i+end_marker_zeros<=size
			&& ret[i+end_marker_zeros-1]!=0)
		{
			i+=end_marker_zeros;
			continue;
		}
		
		if(ch==0)
		{
			++end_marker_state;

			if(end_marker_state==end_marker_zeros)
			{
				char ich=2;
				ret.insert(ret.begin()+i+1, ich);
				++i;
				end_marker_state=0;
				Server->Log("Escaped something at "+nconvert(i));
			}
		}
		else
		{
			end_marker_state=0;
		}

		++i;
	}
}

