#include <vector>

#include "socket_header.h"
#include "types.h"
#include "libfastcgi/fastcgi.hpp"

class CSelectThread;
class CClient;

class CAcceptThread
{
public:
	CAcceptThread(unsigned int nWorkerThreadsPerMaster, unsigned short int uPort);
	~CAcceptThread();

	void operator()(bool single=false);

private:
	void AddToSelectThread(CClient *client);

	std::vector<CSelectThread*> SelectThreads;

	SOCKET s;
	unsigned int WorkerThreadsPerMaster;
};

class OutputCallback : public FCGIProtocolDriver::OutputCallback
{
public:
 
  virtual ~OutputCallback();
  OutputCallback(SOCKET fd_);

  virtual void operator() (const void* buf, size_t count);
private:
  SOCKET fd;
};
