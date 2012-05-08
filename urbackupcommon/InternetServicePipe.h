#include "../Interface/Pipe.h"

class IAESEncryption;
class IAESDecryption;

class InternetServicePipe : public IPipe
{
public:
	InternetServicePipe(IPipe *cs, const std::string &key);
	~InternetServicePipe(void);

	virtual size_t Read(char *buffer, size_t bsize, int timeoutms=-1);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms=-1);
	virtual size_t Read(std::string *ret, int timeoutms=-1);
	virtual bool Write(const std::string &str, int timeoutms=-1);

	std::string decrypt(const std::string &data);
	std::string encrypt(const std::string &data);

	/**
	* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
	*/
	virtual bool isWritable(int timeoutms=0);
	virtual bool isReadable(int timeoutms=0);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumElements(void);

	void destroyBackendPipeOnDelete(bool b);
	void setBackendPipe(IPipe *pCS);

	IPipe *getRealPipe(void);

	virtual void setThrottle(size_t bps);

	virtual size_t getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

private:
	IPipe *cs;

	IAESEncryption *enc;
	IAESDecryption *dec;

	bool destroy_cs;
};