#include "../Interface/Types.h"
#include "../Interface/Thread.h"

class IPipe;

class CHTTPAction : public IThread
{
public:
	CHTTPAction(const std::wstring &pName, const std::wstring pContext, const std::string &pGETStr, const std::string pPOSTStr, const str_nmap &pRawPARAMS, IPipe *pOutput);

	void operator()(void);
private:

	std::wstring name;
	std::string GETStr;
	std::string POSTStr;
	str_nmap RawPARAMS;
	std::wstring context;

	IPipe *output;

};