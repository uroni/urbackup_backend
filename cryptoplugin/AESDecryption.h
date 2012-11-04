#include <string>
#include "cryptopp_inc.h"

#include "IAESDecryption.h"

class AESDecryption : public IAESDecryption
{
public:
	AESDecryption(const std::string &password, bool hash_password);
	~AESDecryption();

	std::string decrypt(const std::string &data);
	size_t decrypt(char *data, size_t data_size);

private:
	CryptoPP::SecByteBlock m_sbbKey;

	CryptoPP::CFB_Mode<CryptoPP::AES>::Decryption *dec;

	std::string iv_buffer;
};