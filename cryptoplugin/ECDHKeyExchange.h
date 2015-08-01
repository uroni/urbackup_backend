#pragma once
#include "IECDHKeyExchange.h"
#include "cryptopp_inc.h"

class ECDHKeyExchange : public IECDHKeyExchange
{
public:
	ECDHKeyExchange();
	
	virtual std::string getPublicKey();

	virtual std::string getSharedKey( const std::string& other_public );

private:
	CryptoPP::SecByteBlock priv;
	CryptoPP::SecByteBlock pub;
};