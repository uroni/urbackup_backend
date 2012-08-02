#include <string>
#ifdef _WIN32
#include <aes.h>
#include <sha.h>
#include <modes.h>
#else
#include <crypto++/aes.h>
#include <crypto++/sha.h>
#include <crypto++/modes.h>
#endif

#include "IAESEncryption.h"

class AESEncryption : public IAESEncryption
{
public:
	AESEncryption(const std::string &password);
	~AESEncryption();

	std::string encrypt(const std::string &data);
	virtual std::string encrypt(char *data, size_t data_size);
	virtual std::string encrypt(const char *data, size_t data_size);

private:

	bool iv_done;
	CryptoPP::SecByteBlock m_sbbKey;
	CryptoPP::SecByteBlock m_IV;

	CryptoPP::CFB_Mode<CryptoPP::AES>::Encryption *enc;
};