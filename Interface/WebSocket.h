#pragma once

#include "Types.h"
#include "Object.h"
#include "Pipe.h"

class IWebSocket : public IObject
{
public:
	virtual void Execute(str_map& GET, THREAD_ID tid, str_map& PARAMS, IPipe* pipe, const std::string& endpoint_name) = 0;
	virtual std::string getName() = 0;
};
