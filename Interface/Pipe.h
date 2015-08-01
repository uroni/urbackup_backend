#ifndef IPIPE_H
#define IPIPE_H

#include <string>

#include "Object.h"
#include "Types.h"

class IPipeThrottler;

class IPipe : public IObject
{
public:
	/**
	* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: blocking
	*/
	virtual size_t Read(char *buffer, size_t bsize, int timeoutms=-1)=0;
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms=-1, bool flush=true)=0;
	virtual size_t Read(std::string *ret, int timeoutms=-1)=0;
	virtual bool Write(const std::string &str, int timeoutms=-1, bool flush=true)=0;

	virtual bool Flush(int timeoutms=-1)=0;

	/**
	* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
	*/
	virtual bool isWritable(int timeoutms=0)=0;
	virtual bool isReadable(int timeoutms=0)=0;

	virtual bool hasError(void)=0;

	virtual void shutdown(void)=0;

	/**
	* only works with memory pipe
	**/
	virtual size_t getNumElements(void)=0;

	virtual void addThrottler(IPipeThrottler *throttler)=0;
	virtual void addOutgoingThrottler(IPipeThrottler *throttler)=0;
	virtual void addIncomingThrottler(IPipeThrottler *throttler)=0;

	virtual _i64 getTransferedBytes(void)=0;
	virtual void resetTransferedBytes(void)=0;
};

#endif //IPIPE_H
