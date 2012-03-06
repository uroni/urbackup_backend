#ifndef INTERFACE_CUSTOMCLIENT_H
#define INTERFACE_CUSTOMCLIENT_H

#include "Object.h"
#include "Types.h"

class IPipe;

class ICustomClient : public IObject
{
public:
	virtual void Init(THREAD_ID pTID, IPipe *pPipe)=0;

	virtual bool Run(void)=0;
	virtual void ReceivePackets(void)=0;

	virtual bool wantReceive(void){ return true; }
	virtual bool closeSocket(void){ return true; }
};

#endif
