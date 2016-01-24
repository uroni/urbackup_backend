#ifndef INTERFACE_CUSTOMCLIENT_H
#define INTERFACE_CUSTOMCLIENT_H

#include "Object.h"
#include "Types.h"

class IPipe;

class IRunOtherCallback
{
public:
	virtual void runOther() = 0;
};

class ICustomClient : public IObject
{
public:
	virtual void Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName)=0;

	virtual bool Run(IRunOtherCallback* run_other)=0;
	virtual void ReceivePackets(IRunOtherCallback* run_other)=0;

	virtual bool wantReceive(void){ return true; }
	virtual bool closeSocket(void){ return true; }
};

#endif
