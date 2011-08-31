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

#include "AESEncryption.h"

AESEncryption::AESEncryption(const std::string &password)
{
	m_sbbKey.resize(CryptoPP::SHA256::DIGESTSIZE);
	CryptoPP::SHA256().CalculateDigest(m_sbbKey, (byte*)password.c_str(), password.size() );

	m_IV.resize(16);

	for(int i=0;i<16;++i)
	{
		m_IV[i]=rand()%256;
	}

	iv_done=false;

	enc=new CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption(m_sbbKey.begin(),m_sbbKey.size(), m_IV.begin() );
}

AESEncryption::~AESEncryption()
{
	delete enc;
}

std::string AESEncryption::encrypt(const std::string &data)
{
	std::string ret;
	if(iv_done==false)
	{
		ret.resize(16);
		memcpy((char*)ret.c_str(), m_IV.BytePtr(), 16);
	}

	size_t osize=ret.size();
	ret.resize(osize+data.size());
	if(data.size()>0)
	{
		enc->ProcessString((byte*)&ret[osize], (byte*)data.c_str(), data.size() );
	}
	return ret;
}