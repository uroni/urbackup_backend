#include "ICryptoFactory.h"

class CryptoFactory : public ICryptoFactory
{
public:
	virtual IAESEncryption* createAESEncryption(const std::string &password);
	virtual IAESDecryption* createAESDecryption(const std::string &password);
	virtual bool generatePrivatePublicKeyPair(const std::string &keybasename);
	virtual bool signFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename);
	virtual bool verifyFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename);
	virtual std::string generatePasswordHash(const std::string &password, const std::string &salt, size_t iterations=10000);
};