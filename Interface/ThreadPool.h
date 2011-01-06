#ifndef ITHREADPOOL_H_
#define ITHREADPOOL_H_

#include "Types.h"

class IThread;

class IThreadPool
{
public:
	virtual THREADPOOL_TICKET execute(IThread *runnable)=0;
	virtual bool isRunning(THREADPOOL_TICKET ticket)=0;
	virtual void waitFor(std::vector<THREADPOOL_TICKET> tickets)=0;
	virtual void waitFor(THREADPOOL_TICKET ticket)=0;
};

#endif //ITHREADPOOL_H_