#include "Interface/Pipe.h"
#include "socket_header.h"
#include <vector>

class CStreamPipe : public IPipe
{
public:
	CStreamPipe( SOCKET pSocket);
	~CStreamPipe();

	virtual size_t Read(char *buffer, size_t bsize, int timeoutms);
	virtual bool Write(const char *buffer, size_t bsize, int timeoutms);
	virtual size_t Read(std::string *ret, int timeoutms);
	virtual bool Write(const std::string &str, int timeoutms);

	virtual bool isWritable(int timeoutms);
	virtual bool isReadable(int timeoutms);

	virtual bool hasError(void);

	virtual void shutdown(void);

	virtual size_t getNumElements(void){ return 0;};

	SOCKET getSocket(void);

	virtual void addThrottler(IPipeThrottler *throttler);
	virtual void addOutgoingThrottler(IPipeThrottler *throttler);
	virtual void addIncomingThrottler(IPipeThrottler *throttler);

	virtual size_t getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

private:
	SOCKET s;
	void doThrottle(size_t new_bytes, bool outgoing);

	size_t transfered_bytes;

	bool has_error;

	std::vector<IPipeThrottler*> incoming_throttlers;
	std::vector<IPipeThrottler*> outgoing_throttlers;
};
