#pragma once

#include "../Interface/Pipe.h"

class IAESEncryption;
class IAESDecryption;

#ifndef HAS_IINTERNET_SERVICE_PIPE
#define HAS_IINTERNET_SERVICE_PIPE
class IInternetServicePipe : public IPipe
{
public:
	virtual std::string decrypt(const std::string &data) = 0;
	virtual std::string encrypt(const std::string &data) = 0;

	virtual void destroyBackendPipeOnDelete(bool b)=0;
	virtual void setBackendPipe(IPipe *pCS)=0;

	virtual IPipe *getRealPipe(void)=0;
};
#endif

class InternetServicePipe : public IInternetServicePipe
{
public:
	InternetServicePipe(void);
	InternetServicePipe(IPipe *cs, const std::string &key);
	~InternetServicePipe(void);

	void init(IPipe *pcs, const std::string &key);

	virtual size_t Read(char *buffer, size_t bsize, int timeoutms=-1);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms=-1, bool flush=true);
	virtual size_t Read(std::string *ret, int timeoutms=-1);
	virtual bool Write(const std::string &str, int timeoutms=-1, bool flush=true);

	virtual bool Flush(int timeoutms=-1);

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

	virtual void addThrottler(IPipeThrottler *throttler);
	virtual void addOutgoingThrottler(IPipeThrottler *throttler);
	virtual void addIncomingThrottler(IPipeThrottler *throttler);

	virtual _i64 getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

private:
	IPipe *cs;

	IAESEncryption *enc;
	IAESDecryption *dec;

	bool destroy_cs;
};