#pragma once

#include "../Interface/CustomClient.h"
#include "../Interface/Service.h"
#include "../Interface/ThreadPool.h"
#include <memory>

class SambaServiceFactory : public IService
{
public:
	virtual ICustomClient* createClient();
	virtual void destroyClient(ICustomClient* pClient);
};


class SambaService : public ICustomClient
{
public:
	void Init(THREAD_ID pTID, IPipe* pPipe, const std::string& pEndpointName, const IClientWakeup* wakeup) override;
	int Run() override;
	void ReceivePackets() override;
	virtual bool wantReceive() override;

private:

	enum class State
	{
		Init,
		Shutdown,
		Running
	};

	State state;
	THREADPOOL_TICKET readTicket;

	IPipe* pipe;
	std::unique_ptr<IPipe> smbPipe;
};