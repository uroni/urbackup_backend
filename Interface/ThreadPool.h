#ifndef ITHREADPOOL_H_
#define ITHREADPOOL_H_

#include "Types.h"
#include "Object.h"

class IThread;

class IThreadPool : public IObject
{
public:
	virtual THREADPOOL_TICKET execute(IThread *runnable, const std::string& name = std::string())=0;
	virtual void executeWait(IThread *runnable, const std::string& name = std::string())=0;
	virtual bool isRunning(THREADPOOL_TICKET ticket)=0;
	virtual bool waitFor(std::vector<THREADPOOL_TICKET> tickets, int timems=-1)=0;
	virtual bool waitFor(THREADPOOL_TICKET ticket, int timems=-1)=0;
	virtual void Shutdown() = 0;
};

#endif //ITHREADPOOL_H_