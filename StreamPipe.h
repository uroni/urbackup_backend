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

private:
	SOCKET s;

	bool has_error;
};
