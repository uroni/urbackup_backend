/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "AESDecryption.h"

AESDecryption::AESDecryption(const std::string &password)
{
	m_sbbKey.resize(CryptoPP::SHA256::DIGESTSIZE);
	CryptoPP::SHA256().CalculateDigest(m_sbbKey, (byte*)password.c_str(), password.size() );

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
		if(!iv_buffer.empty() &&  iv_buffer.size()+data.size()>=16)
		{
			CryptoPP::SecByteBlock m_IV;
			m_IV.resize(16);
			memcpy(m_IV.BytePtr(), &iv_buffer[0], 16);
			memcpy(m_IV.BytePtr()+iv_buffer.size(), &data[0], 16-iv_buffer.size());
			dec=new CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
			done=16-iv_buffer.size();
		}
		else if(data.size()>=16)
		{
			CryptoPP::SecByteBlock m_IV;
			m_IV.resize(16);
			memcpy(m_IV.BytePtr(), &data[0], 16);
			dec=new CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
			done=16;
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