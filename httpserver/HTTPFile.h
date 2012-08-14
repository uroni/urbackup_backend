#include "../Interface/Thread.h"
#include "../Interface/Object.h"

#include <string>

class IPipe;

class CHTTPFile : public IThread, public IObject
{
public:
	CHTTPFile(std::string pFilename, IPipe *pOutput);
	std::string getContentType(void);
	std::string getIndexFiles(void);
	void operator()(void);

private:
	std::string filename;
	IPipe *output;
};
