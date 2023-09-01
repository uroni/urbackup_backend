#pragma once

#include "Interface/Pipe.h"
#include "Interface/Mutex.h"
#include "socket_header.h"
#include <vector>

class CStreamPipe : public IPipe
{
public:
	CStreamPipe( SOCKET pSocket, const std::string& usage_str);
	~CStreamPipe();

	virtual size_t Read(char *buffer, size_t bsize, int timeoutms);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms, bool flush);
	virtual size_t Read(std::string *ret, int timeoutms);
	virtual bool Write(const std::string &str, int timeoutms, bool flush);

	virtual bool isWritable(int timeoutms);
	virtual bool isReadable(int timeoutms);
	virtual bool isReadOrWritable(int timeoutms);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumWaiters() {
		return 0;
	};
	virtual size_t getNumElements(void){ return 0;};

	SOCKET getSocket(void);

	virtual void addThrottler(IPipeThrottler *throttler);
	virtual void addOutgoingThrottler(IPipeThrottler *throttler);
	virtual void addIncomingThrottler(IPipeThrottler *throttler);

	virtual _i64 getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

	virtual bool Flush( int timeoutms=-1 );

	bool doThrottle(size_t new_bytes, bool outgoing, bool wait);

	static void init_mutex();

	virtual void setUsageString(const std::string& str);

	static std::vector<std::string> getPipeList();

	virtual bool setCompressionSettings(const SCompressionSettings& params);

private:
	SOCKET s;

	_i64 transfered_bytes;

	bool has_error;

	std::vector<IPipeThrottler*> incoming_throttlers;
	std::vector<IPipeThrottler*> outgoing_throttlers;

	static std::map<CStreamPipe*, std::string> active_pipes;
	static IMutex* active_pipes_mutex;
};
