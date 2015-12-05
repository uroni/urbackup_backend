#pragma once
#include "IAESGCMEncryption.h"
#include "cryptopp_inc.h"
#include <vector>

class AESGCMEncryption : public IAESGCMEncryption
{
public:
	AESGCMEncryption(const std::string& key, bool hash_password);

	virtual void put( const char *data, size_t data_size );

	virtual void flush();

	virtual std::string get();

	virtual int64 getOverheadBytes();

private:
	void reinit();
	void decEndMarkers(size_t n);
	void escapeEndMarker(std::string& ret, size_t size, size_t offset);

	size_t end_marker_state;
	bool iv_done;
	CryptoPP::SecByteBlock m_sbbKey;
	CryptoPP::SecByteBlock m_IV;

	CryptoPP::GCM<CryptoPP::AES >::Encryption encryption;
	CryptoPP::AuthenticatedEncryptionFilter encryption_filter;
	std::vector<size_t> end_markers;
	int64 overhead_size;
	size_t message_size;
};