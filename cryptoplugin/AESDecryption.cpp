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

#include "AESDecryption.h"

AESDecryption::AESDecryption(const std::string &password, bool hash_password)
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

	dec=NULL;
}

AESDecryption::~AESDecryption()
{
	delete dec;
}

std::string AESDecryption::decrypt(const std::string &data)
{
	if(dec==NULL)
	{
		size_t done=0;
		if(!iv_buffer.empty() &&  iv_buffer.size()+data.size()>=CryptoPP::AES::BLOCKSIZE)
		{
			CryptoPP::SecByteBlock m_IV;
			m_IV.resize(CryptoPP::AES::BLOCKSIZE);
			memcpy(m_IV.BytePtr(), &iv_buffer[0], CryptoPP::AES::BLOCKSIZE);
			memcpy(m_IV.BytePtr()+iv_buffer.size(), &data[0], CryptoPP::AES::BLOCKSIZE-iv_buffer.size());
			dec=new CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
			done=CryptoPP::AES::BLOCKSIZE-iv_buffer.size();
		}
		else if(data.size()>=CryptoPP::AES::BLOCKSIZE)
		{
			CryptoPP::SecByteBlock m_IV;
			m_IV.resize(CryptoPP::AES::BLOCKSIZE);
			memcpy(m_IV.BytePtr(), &data[0], CryptoPP::AES::BLOCKSIZE);
			dec=new CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
			done=CryptoPP::AES::BLOCKSIZE;
		}
		else
		{
			iv_buffer+=data;
			done=data.size();
		}
		if(done<data.size())
		{
			std::string ret;
			ret.resize(data.size()-done);
			dec->ProcessString((byte*)&ret[0], (byte*)&data[done], ret.size() );
			return ret;
		}
		else
		{
			return "";
		}
	}
	else
	{
		std::string ret;
		ret.resize(data.size());
		dec->ProcessString((byte*)&ret[0], (byte*)&data[0], data.size() );
		return ret;
	}
}

size_t AESDecryption::decrypt(char *data, size_t data_size)
{
	size_t offset=0;
	if(dec==NULL)
	{
		size_t done=0;
		if(!iv_buffer.empty() &&  iv_buffer.size()+data_size>=CryptoPP::AES::BLOCKSIZE)
		{
			CryptoPP::SecByteBlock m_IV;
			m_IV.resize(CryptoPP::AES::BLOCKSIZE);
			memcpy(m_IV.BytePtr(), &iv_buffer[0], CryptoPP::AES::BLOCKSIZE);
			memcpy(m_IV.BytePtr()+iv_buffer.size(), data, CryptoPP::AES::BLOCKSIZE-iv_buffer.size());
			dec=new CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
			done=CryptoPP::AES::BLOCKSIZE-iv_buffer.size();
		}
		else if(iv_buffer.empty() && data_size>=CryptoPP::AES::BLOCKSIZE)
		{
			CryptoPP::SecByteBlock m_IV;
			m_IV.resize(CryptoPP::AES::BLOCKSIZE);
			memcpy(m_IV.BytePtr(), data, CryptoPP::AES::BLOCKSIZE);
			dec=new CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
			done=CryptoPP::AES::BLOCKSIZE;
		}
		else
		{
			std::string db;
			db.resize(data_size);
			memcpy((char*)db.c_str(), data, data_size);
			iv_buffer+=db;
			done=data_size;
		}
		if(done<data_size)
		{
			dec->ProcessString((byte*)(data+done), data_size-done);
		}
		return done;
	}
	else
	{
		dec->ProcessString((byte*)data, data_size);
		return 0;
	}
}