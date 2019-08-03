#pragma once
#ifdef WITH_OPENSSL

#include "Interface/Pipe.h"
#include "StreamPipe.h"
#include <memory>
#include <openssl/bio.h>

class OpenSSLPipe : public IPipe
{
public:
	OpenSSLPipe(CStreamPipe* bpipe);

	~OpenSSLPipe();

	static void init();

	bool ssl_connect(const std::string& p_hostname, int timeoutms);

	// Inherited via IPipe
	virtual size_t Read(char * buffer, size_t bsize, int timeoutms = -1);

	virtual bool Write(const char * buffer, size_t bsize, int timeoutms = -1, bool flush = true);

	virtual size_t Read(std::string * ret, int timeoutms = -1);

	virtual bool Write(const std::string & str, int timeoutms = -1, bool flush = true);

	virtual bool Flush(int timeoutms = -1);

	virtual bool isWritable(int timeoutms = 0);

	virtual bool isReadable(int timeoutms = 0);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumElements(void);

	virtual void addThrottler(IPipeThrottler * throttler);

	virtual void addOutgoingThrottler(IPipeThrottler * throttler);

	virtual void addIncomingThrottler(IPipeThrottler * throttler);

	virtual _i64 getTransferedBytes(void);

	virtual void resetTransferedBytes(void);

private:

	std::auto_ptr<CStreamPipe> bpipe;

	BIO* bbio;
	SSL_CTX* ctx;

	bool has_error;
};

#endif //WITH_OPENSSL