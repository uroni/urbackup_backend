/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#define VLOG(x)

const size_t iv_size = 12;
const size_t end_marker_zeros = 4;

using namespace CryptoPPCompat;

AESGCMEncryption::AESGCMEncryption( const std::string& key, bool hash_password)
	: encryption(), encryption_filter(encryption), iv_done(false), end_marker_state(0),
	overhead_size(0), message_size(0)
{
	if(hash_password)
	{
		m_sbbKey.resize(CryptoPP::SHA256::DIGESTSIZE);
		CryptoPP::SHA256().CalculateDigest(m_sbbKey.BytePtr(), reinterpret_cast<const byte*>(key.data()), key.size() );
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

	m_orig_IV = m_IV;

	iv_done=false;

	assert(encryption.CanUseStructuredIVs());
	assert(encryption.IsResynchronizable());
}


void AESGCMEncryption::put( const char *data, size_t data_size )
{
	Server->Log("AESGCMEncryption::put " + std::string(data, data_size));
	encryption_filter.Put(reinterpret_cast<const byte*>(data), data_size);
	message_size+=data_size;

	if (message_size > 2LL * 1024 * 1024 * 1024 - 1)
	{
		flush();
	}
}

void AESGCMEncryption::flush()
{
	encryption_filter.MessageEnd();
	end_markers.push_back(encryption_filter.MaxRetrievable());
	CryptoPP::IncrementCounterByOne(m_IV.BytePtr(), static_cast<unsigned int>(m_IV.size()));
	encryption.Resynchronize(m_IV.BytePtr(), static_cast<int>(m_IV.size()));
	overhead_size+=16; //tag size
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
		memcpy(&ret[0], m_orig_IV.BytePtr(), m_orig_IV.size());
		iv_done=true;
		overhead_size+=m_orig_IV.size();
	}

	if(max_retrievable>0)
	{
		size_t nb = encryption_filter.Get(reinterpret_cast<byte*>(&ret[iv_add]), max_retrievable);
		assert(nb==max_retrievable);
		/*if(nb!=max_retrievable)
		{
			ret.resize(nb+iv_add+ ( add_end_marker ? (end_marker_zeros + 1) : 0 ));
		}*/
		escapeEndMarker(ret, iv_add+nb, iv_add);
		decEndMarkers(nb);
	}

	if(add_end_marker)	
	{
		//The rest is already zero
		ret[ret.size()-1]=1;
		end_marker_state=0;
		overhead_size+=end_marker_zeros+1;
		message_size+=end_marker_zeros+1;
		encryption_filter.GetNextMessage();
		VLOG(Server->Log("New message. Size: "+convert(message_size), LL_DEBUG));
		message_size=0;
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
				Server->Log("Escaped something at "+convert(i), LL_DEBUG);
				++overhead_size;
			}
		}
		else
		{
			end_marker_state=0;
		}

		++i;
	}
}

int64 AESGCMEncryption::getOverheadBytes()
{
	return overhead_size;
}

