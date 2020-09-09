#pragma once

#include "IECIES.h"
#include "cryptopp_inc.h"
#include <memory>

class ECIESEncryption : public IECIESEncryption
{
public:
	ECIESEncryption(const std::string& pubkey);
	virtual std::string encrypt(const std::string& msg);

	bool has_error;
private:
	CryptoPP::ECIES<CryptoPP::ECP, CryptoPP::SHA256, CryptoPP::IncompatibleCofactorMultiplication>::Encryptor encryptor;
};

class ECIESDecryption : public IECIESDecryption
{
public:
	ECIESDecryption();

	virtual std::string decrypt(const std::string& msg);
	virtual std::string getPublicKey();

	bool has_error;
private:
	CryptoPP::ECIES<CryptoPP::ECP, CryptoPP::SHA256, CryptoPP::IncompatibleCofactorMultiplication>::Decryptor decryptor;
};