#include <string>
#include "IAESEncryption.h"
#include "IAESDecryption.h"
#include "IAESGCMDecryption.h"
#include "IAESGCMEncryption.h"
#include "IZlibCompression.h"
#include "IZlibDecompression.h"
#include "../Interface/Plugin.h"
#include "IECDHKeyExchange.h"

class ICryptoFactory: public IPlugin
{
public:
	virtual IAESEncryption* createAESEncryption(const std::string &password)=0;
	virtual IAESDecryption* createAESDecryption(const std::string &password)=0;
	virtual IAESEncryption* createAESEncryptionNoDerivation(const std::string &password)=0;
	virtual IAESDecryption* createAESDecryptionNoDerivation(const std::string &password)=0;
	virtual IAESGCMEncryption* createAESGCMEncryption(const std::string &password)=0;
	virtual IAESGCMDecryption* createAESGCMDecryption(const std::string &password)=0;
	virtual IAESGCMEncryption* createAESGCMEncryptionNoDerivation(const std::string &password)=0;
	virtual IAESGCMDecryption* createAESGCMDecryptionNoDerivation(const std::string &password)=0;
	virtual IZlibCompression* createZlibCompression(int compression_level)=0;
	virtual IZlibDecompression* createZlibDecompression(void)=0;
	virtual bool generatePrivatePublicKeyPair(const std::string &name)=0;
	virtual bool generatePrivatePublicKeyPairDSA(const std::string &name)=0;
	virtual bool signFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
	virtual bool signFileDSA(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
	virtual bool signData(const std::string &pubkey, const std::string &data, std::string &signature)=0;
	virtual bool signDataDSA(const std::string &pubkey, const std::string &data, std::string &signature)=0;
	virtual bool verifyFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
	virtual bool verifyData(const std::string &pubkey, const std::string &data, const std::string &signature)=0;
	virtual bool verifyDataDSA(const std::string &pubkey, const std::string &data, const std::string &signature)=0;
	//PBKDF2-SHA256 Hex
	virtual std::string generatePasswordHash(const std::string &password, const std::string &salt, size_t iterations=10000)=0;

	//PBKDF2-SHA512 Binary
	virtual std::string generateBinaryPasswordHash(const std::string &password, const std::string &salt, size_t iterations=10000)=0;

	virtual std::string encryptAuthenticatedAES(const std::string& data, const std::string &password, size_t iterations=20000)=0;
	virtual std::string decryptAuthenticatedAES(const std::string& data, const std::string &password, size_t iterations=20000)=0;
	virtual IECDHKeyExchange* createECDHKeyExchange()=0;
};
