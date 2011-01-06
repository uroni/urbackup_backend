#include <string>
#include "IAESEncryption.h"
#include "IAESDecryption.h"
#include "../Interface/Plugin.h"

class ICryptoFactory: public IPlugin
{
public:
	virtual IAESEncryption* createAESEncryption(const std::string &password)=0;
	virtual IAESDecryption* createAESDecryption(const std::string &password)=0;
	virtual bool generatePrivatePublicKeyPair(const std::string &name)=0;
	virtual bool signFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
	virtual bool verifyFile(const std::string &keyfilename, const std::string &filename, const std::string &sigfilename)=0;
};
