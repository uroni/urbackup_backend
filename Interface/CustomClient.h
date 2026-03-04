#ifndef INTERFACE_CUSTOMCLIENT_H
#define INTERFACE_CUSTOMCLIENT_H

#include "Object.h"
#include "Types.h"
#include "Pipe.h"

class IClientWakeup
{
public:
	virtual void wakeup() const = 0;
};

class PipeWakeupWrapper : public IClientWakeup
{
	IPipe* pipe;
public:
	PipeWakeupWrapper(IPipe* pipe)
		: pipe(pipe) {
		pipe->setOption(IPipe::SocketOption_CanWakeup);
	}

	virtual void wakeup() const {
		pipe->wakeupRead();
	}
};

class ICustomClient : public IObject
{
public:
	virtual void Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName, const IClientWakeup* wakeup)=0;

	virtual int Run()=0;
	virtual void ReceivePackets()=0;

	virtual bool wantReceive(void){ return true; }
	virtual bool closeSocket(void){ return true; }
};

#endif
