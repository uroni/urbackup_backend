#include "../Interface/Thread.h"
#include "../Interface/Object.h"
#include "HTTPRequestDone.h"

#include <string>

class IPipe;

class CHTTPFile : public IThread, public IObject
{
public:
	CHTTPFile(std::string pFilename, IPipe *pOutput, CHTTPRequestDone* pRequestDone);
	std::string getContentType(void);
	std::string getIndexFiles(void);
	void operator()(void);

private:
	std::string filename;
	IPipe *output;
	CHTTPRequestDone* request_done;
};
