#include "../Interface/Types.h"
#include "../Interface/Thread.h"
#include "../Interface/Object.h"

class IPipe;

class CHTTPAction : public IThread, public IObject
{
public:
	CHTTPAction(const std::string &pName, const std::string pContext, const std::string &pGETStr, const std::string pPOSTStr, const str_map &pRawPARAMS, IPipe *pOutput);

	void operator()(void);
private:

	std::string name;
	std::string GETStr;
	std::string POSTStr;
	str_map RawPARAMS;
	std::string context;

	IPipe *output;

};
