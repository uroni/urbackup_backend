#include "Interface/Pipe.h"
#include "socket_header.h"

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

	virtual void setThrottle(size_t bps);

	virtual size_t getTransferedBytes(void);
	virtual void resetTransferedBytes(void);

private:
	SOCKET s;

	void doThrottle(size_t new_bytes);
	size_t curr_bytes;
	unsigned int lastresettime;

	size_t transfered_bytes;


	bool has_error;

	size_t throttle_bps;
};
