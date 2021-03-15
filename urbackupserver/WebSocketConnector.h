#pragma once

#include "../Interface/WebSocket.h"
#include "../Interface/Service.h"

class WebSocketConnector : public IWebSocket
{
public:
	WebSocketConnector(IService* wrapped_service, const std::string& name)
		: wrapped_service(wrapped_service),
		name(name) {}

	virtual void Execute(str_map& GET, THREAD_ID tid, str_map& PARAMS, IPipe* pipe, const std::string& endpoint_name);
	virtual std::string getName();

private:
	IService* wrapped_service;
	std::string name;
};