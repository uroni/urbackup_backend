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

#include "IAESDecryption.h"

class AESDecryption : public IAESDecryption
{
public:
	AESDecryption(const std::string &password);
	~AESDecryption();

	std::string decrypt(const std::string &data);
	size_t decrypt(char *data, size_t data_size);

private:
	CryptoPP::SecByteBlock m_sbbKey;

	CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption *dec;

	std::string iv_buffer;
};