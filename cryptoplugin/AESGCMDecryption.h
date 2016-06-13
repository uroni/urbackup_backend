#pragma once
#include "IAESGCMDecryption.h"
#include "cryptopp_inc.h"

class AESGCMDecryption : public IAESGCMDecryption
{
public:
	AESGCMDecryption(const std::string &password, bool hash_password);

	virtual bool put( const char *data, size_t data_size );

	virtual std::string get( bool& has_error );

	virtual bool get(char *data, size_t& data_size);

	virtual int64 getOverheadBytes();

	virtual bool hasData();

private:
	size_t findAndUnescapeEndMarker(const char *data, size_t data_size,
		std::string& data_copy, bool& has_copy, bool& has_error,
		size_t& escaped_zeros);

	CryptoPP::GCM<CryptoPP::AES >::Decryption decryption;
	CryptoPP::AuthenticatedDecryptionFilter decryption_filter;

	CryptoPP::SecByteBlock m_sbbKey;
	std::string iv_buffer;
	bool iv_done;

	size_t end_marker_state;

	int64 overhead_bytes;
};