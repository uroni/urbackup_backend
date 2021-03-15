#pragma once

#include "../Interface/Types.h"
#include "../Interface/Thread.h"
#include "../Interface/Object.h"

class IPipe;

class CHTTPSocket : public IThread, public IObject
{
public:
	CHTTPSocket(const std::string& name, const std::string& gparams, const str_map& pRawPARAMS, IPipe* pOutput, const std::string& endpoint_name);

	void operator()();
private:
	std::string name;
	std::string gparams;
	str_map RawPARAMS;
	std::string endpoint_name;

	IPipe* output;

};
