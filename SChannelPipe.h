#pragma once

#include "Interface/Pipe.h"
#include "StreamPipe.h"
#define SECURITY_WIN32
#include <Windows.h>
#include <security.h>

class SChannelPipe : public IPipe
{
public:
	SChannelPipe(CStreamPipe* bpipe);

	~SChannelPipe();

	bool ssl_connect(const std::string& p_hostname, int timeoutms);

	static void init();

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

	virtual size_t getNumWaiters();

	virtual size_t getNumElements(void);

	virtual void addThrottler(IPipeThrottler * throttler);

	virtual void addOutgoingThrottler(IPipeThrottler * throttler);

	virtual void addIncomingThrottler(IPipeThrottler * throttler);

	virtual _i64 getTransferedBytes(void);

	virtual void resetTransferedBytes(void);

private:
	bool ssl_connect_negotiate(int timeoutms, bool do_read);

	CStreamPipe* bpipe;

	static PSecurityFunctionTableW sec;

	bool has_cred_handle;
	CredHandle cred_handle;
	TimeStamp time_stamp;
	bool has_ctxt_handle;
	CtxtHandle ctxt_handle;
	std::vector<char> encbuf;
	size_t encbuf_pos;
	std::vector<char> decbuf;
	size_t decbuf_pos;
	std::vector<char> sendbuf;
	size_t sendbuf_pos;
	std::string hostname;
	int64 last_flush_time;
	std::vector<char> header_buf;
	std::vector<char> trailer_buf;
	bool has_error;

	SecPkgContext_StreamSizes stream_sizes;
};
