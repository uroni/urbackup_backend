#pragma once
#include "IAESGCMEncryption.h"
#include "cryptopp_inc.h"

class AESGCMEncryption : public IAESGCMEncryption
{
public:
	AESGCMEncryption(const std::string& key, bool hash_password);

	virtual void put( const char *data, size_t data_size );

	virtual void flush();

	virtual std::string get();

private:

	bool iv_done;
	CryptoPP::SecByteBlock m_sbbKey;
	CryptoPP::SecByteBlock m_IV;

	CryptoPP::GCM<CryptoPP::AES >::Encryption encryption;
	CryptoPP::AuthenticatedEncryptionFilter encryption_filter;
};