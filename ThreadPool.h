#include "Interface/Mutex.h"
#include "Interface/Condition.h"
#include "Interface/Thread.h"
#include <deque>

#include "Interface/ThreadPool.h"

class IThread;
class CThreadPool;

class CPoolThread : public IThread
{
public:
	CPoolThread(CThreadPool *pMgr);

	void operator()(void);

	void shutdown(void);

private:

	volatile bool dexit;
	CThreadPool* mgr;
};


class CThreadPool : public IThreadPool
{
public:
	CThreadPool();
	~CThreadPool();

	THREADPOOL_TICKET execute(IThread *runnable);
	void executeWait(IThread *runnable);
	bool isRunning(THREADPOOL_TICKET ticket);
	bool waitFor(std::vector<THREADPOOL_TICKET> tickets, int timems=-1);
	bool waitFor(THREADPOOL_TICKET ticket, int timems=-1);
	void Remove(CPoolThread *pt);

	void Shutdown(void);

private:
	IThread * getRunnable(THREADPOOL_TICKET *todel, bool del, bool& stop);

	bool isRunningInt(THREADPOOL_TICKET ticket);

	unsigned int nThreads;
	unsigned int nRunning;

	std::vector<CPoolThread*> threads;
	std::deque<std::pair<IThread*, THREADPOOL_TICKET> > toexecute;
	IMutex* mutex;
	ICondition* cond;
	std::map<THREADPOOL_TICKET, ICondition*> running;

	THREADPOOL_TICKET currticket;
	volatile bool dexit;

	friend class CPoolThread;
};