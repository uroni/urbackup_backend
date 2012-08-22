#include <string>
#include "IAESEncryption.h"
#include "IAESDecryption.h"
#include "IZlibCompression.h"
#include "IZlibDecompression.h"
#include "../Interface/Plugin.h"

class ICryptoFactory: public IPlugin
{
public:
	virtual IAESEncryption* createAESEncryption(const std::string &password)=0;
	virtual IAESDecryption* createAESDecryption(const std::string &password)=0;
	virtual IAESEncryption* createAESEncryptionNoDerivation(const std::string &password)=0;
	virtual IAESDecryption* createAESDecryptionNoDerivation(const std::string &password)=0;
	virtual IZlibCompression* createZlibCompression(int compression_level)=0;
	virtual IZlibDecompression* createZlibDecompression(void)=0;
	virtual bool generatePrivatePublicKeyPair(const std::string &name)=0;
	virtual bool signFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
	virtual bool verifyFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
	virtual std::string generatePasswordHash(const std::string &password, const std::string &salt, size_t iterations=10000)=0;
	virtual std::string generateBinaryPasswordHash(const std::string &password, const std::string &salt, size_t iterations=10000)=0;
};
