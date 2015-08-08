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

const size_t iv_size = 64;

AESGCMEncryption::AESGCMEncryption( const std::string& key, bool hash_password)
	: encryption(), encryption_filter(encryption), iv_done(false)
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

	iv_done=false;
}


void AESGCMEncryption::put( const char *data, size_t data_size )
{
	encryption_filter.Put(reinterpret_cast<const byte*>(data), data_size);
}

void AESGCMEncryption::flush()
{
	encryption_filter.MessageEnd();
}

std::string AESGCMEncryption::get()
{
	std::string ret;

	size_t iv_add = 0;

	ret.resize(encryption_filter.MaxRetrievable()+iv_add);

	if(!iv_done)
	{
		iv_add=m_IV.size();
		memcpy(&ret[0], m_IV.BytePtr(), m_IV.size());
		iv_done=true;
	}

	if(ret.size()>iv_add)
	{
		size_t nb = encryption_filter.Get(reinterpret_cast<byte*>(&ret[iv_add]), ret.size()-iv_add);
		if(nb+iv_add!=ret.size())
		{
			ret.resize(nb+iv_add);
		}
	}
	

	return ret;
}

