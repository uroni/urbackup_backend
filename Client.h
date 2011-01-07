#include "socket_header.h"
#include "Interface/Mutex.h"
#include <deque>

class FCGIProtocolDriver;
class OutputCallback;
class FCGIRequest;

class CClient
{
public:
	CClient();
	~CClient();

	SOCKET getSocket();
	OutputCallback *getOutputCallback();
	FCGIProtocolDriver *getFCGIProtocolDriver();

	void addRequest( FCGIRequest* req);
	bool removeRequest( FCGIRequest *req);
	size_t numRequests(void);
	FCGIRequest* getRequest(size_t num);
	FCGIRequest* getAndRemoveReadyRequest(void);

	bool isProcessing(void);
	bool setProcessing(bool b);

	void set(SOCKET ps, OutputCallback *poutput, FCGIProtocolDriver * pdriver );

	void lock();
	void unlock();
	void remove();
private:
	SOCKET s;
	OutputCallback * output;
	FCGIProtocolDriver * driver;
	bool processing;
	IMutex * mutex;
	IScopedLock *m_lock;
	std::deque<FCGIRequest*> requests;
};
