#include "HTTPSocket.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "../Interface/Pipe.h"

CHTTPSocket::CHTTPSocket(const std::string& name, const std::string& gparams, const str_map& pRawPARAMS, IPipe* pOutput, const std::string& endpoint_name)
	: RawPARAMS(pRawPARAMS), output(pOutput), name(name), gparams(gparams), endpoint_name(endpoint_name)
{
}

void CHTTPSocket::operator()()
{
	std::map<std::string, std::string> GET;
	ParseParamStrHttp(gparams, &GET, true);

	THREAD_ID tid = 0;
	tid = Server->ExecuteWebSocket(name, GET, RawPARAMS, output, endpoint_name);
	

	if (tid == ILLEGAL_THREAD_ID)
	{
		std::string error = "Error: Unknown web socket [" + EscapeHTML(name) + "]";
		Server->Log(error, LL_WARNING);
		output->Write("Content-type: text/html; charset=UTF-8\r\n\r\n" + error);
		delete output;
	}
}
