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

#include "ECDHKeyExchange.h"
#include "../Interface/Server.h"

ECDHKeyExchange::ECDHKeyExchange()
{
	CryptoPP::ECDH< CryptoPP::EC2N >::Domain ecdh(CryptoPP::ASN1::sect409k1());

	CryptoPP::AutoSeededRandomPool rng;
	priv.resize(ecdh.PrivateKeyLength());
	pub.resize(ecdh.PublicKeyLength());
	ecdh.GenerateKeyPair(rng, priv, pub);
}

std::string ECDHKeyExchange::getPublicKey()
{
	return std::string(pub.BytePtr(), pub.BytePtr() + pub.size());
}

std::string ECDHKeyExchange::getSharedKey( const std::string& other_public )
{
	CryptoPP::ECDH< CryptoPP::EC2N >::Domain ecdh(CryptoPP::ASN1::sect409k1());

	if(other_public.size()!=ecdh.PublicKeyLength())
	{
		Server->Log("Public key length does not match", LL_ERROR);
		return std::string();
	}

	std::string ret;
	ret.resize(ecdh.AgreedValueLength());
	if(!ecdh.Agree(reinterpret_cast<byte*>(&ret[0]), priv, reinterpret_cast<const byte*>(other_public.data())))
	{
		Server->Log("Failed to agree to ECDH shared secret", LL_ERROR);
		return std::string();
	}

	return ret;
}

