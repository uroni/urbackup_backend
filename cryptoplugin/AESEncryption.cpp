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

#include "AESEncryption.h"
#include "../Interface/Server.h"

AESEncryption::AESEncryption(const std::string &password, bool hash_password)
{
	if(hash_password)
	{
		m_sbbKey.resize(CryptoPP::SHA256::DIGESTSIZE);
		CryptoPP::SHA256().CalculateDigest(m_sbbKey, (byte*)password.c_str(), password.size() );
	}
	else
	{
		m_sbbKey.resize(password.size());
		memcpy(m_sbbKey.BytePtr(), password.c_str(), password.size());
	}

	m_IV.resize(CryptoPP::AES::BLOCKSIZE);

	Server->secureRandomFill((char*)m_IV.BytePtr(), CryptoPP::AES::BLOCKSIZE);

	iv_done=false;

	enc=new CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
}

AESEncryption::~AESEncryption()
{
	delete enc;
}

std::string AESEncryption::encrypt(const std::string &data)
{
	return encrypt(data.c_str(), data.size());
}

std::string AESEncryption::encrypt(const char *data, size_t data_size)
{
	std::string ret;
	if(iv_done==false)
	{
		ret.resize(CryptoPP::AES::BLOCKSIZE);
		memcpy((char*)ret.c_str(), m_IV.BytePtr(), CryptoPP::AES::BLOCKSIZE);
		iv_done=true;
	}

	size_t osize=ret.size();
	ret.resize(osize+data_size);
	if(data_size>0)
	{
		enc->ProcessString((byte*)&ret[osize], (byte*)data, data_size);
	}
	return ret;
}

std::string AESEncryption::encrypt(char *data, size_t data_size)
{
	std::string ret;
	if(iv_done==false)
	{
		ret.resize(CryptoPP::AES::BLOCKSIZE);
		memcpy((char*)ret.c_str(), m_IV.BytePtr(), CryptoPP::AES::BLOCKSIZE);
		iv_done=true;
	}

	if(data_size>0)
	{
		enc->ProcessString((byte*)data, data_size);
	}
	return ret;
}
