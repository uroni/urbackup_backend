#include "../Interface/Types.h"
#include "../Interface/Thread.h"
#include "../Interface/Object.h"

#include <string>
#include <queue>

class IPipe;

struct CBuffer
{
	CBuffer(char *pBuf, size_t pBsize): buf(pBuf), bsize(pBsize) { rcount=0;offset=0;}
	char *buf;
	size_t bsize;
	int *rcount;
	int offset;
};

class CHTTPProxy : public IThread, public IObject
{
public:
	CHTTPProxy(std::string pHttp_method, std::string pHttp_query, int pHttp_version, const std::string pPOSTStr, const str_nmap &pRawPARAMS, IPipe *pOutput, IPipe *pNotify, IPipe *pTimeoutPipe);

	void operator()(void);

private:
	std::string http_method;
	std::string http_query;
	int http_version;
	std::string POSTStr;
	str_nmap RawPARAMS;
	IPipe *notify;
	IPipe *timeoutpipe;

	std::vector<unsigned int> timeouts;
	std::vector<IPipe*> output;
	std::vector<int> sync;
	std::vector< std::queue<CBuffer> > output_buffer;
};
