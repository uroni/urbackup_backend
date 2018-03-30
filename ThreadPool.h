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
	CThreadPool(size_t max_threads, 
		size_t max_waiting_threads,
		std::string idle_name);
	~CThreadPool();

	THREADPOOL_TICKET execute(IThread *runnable, const std::string& name = std::string());
	void executeWait(IThread *runnable, const std::string& name = std::string());
	bool isRunning(THREADPOOL_TICKET ticket);
	bool waitFor(std::vector<THREADPOOL_TICKET> tickets, int timems=-1);
	bool waitFor(THREADPOOL_TICKET ticket, int timems=-1);
	void Remove(CPoolThread *pt, bool decr);

	void Shutdown();

private:
	IThread * getRunnable(THREADPOOL_TICKET *todel, bool del, bool& stop, std::string& name);

	bool isRunningInt(THREADPOOL_TICKET ticket);

	size_t nThreads;
	size_t nRunning;

	std::vector<CPoolThread*> threads;

	struct SNewTask
	{
		SNewTask(IThread* runnable, THREADPOOL_TICKET ticket, std::string name)
			: runnable(runnable), ticket(ticket), name(name)
		{}

		IThread* runnable;
		THREADPOOL_TICKET ticket;
		std::string name;
	};

	std::deque<SNewTask> toexecute;
	IMutex* mutex;
	ICondition* cond;

	struct SRunningConds
	{
		SRunningConds()
			: cond(NULL) {}

		ICondition* cond;
		std::vector<ICondition*> conds;
	};

	std::map<THREADPOOL_TICKET, SRunningConds> running;

	THREADPOOL_TICKET currticket;
	volatile bool dexit;

	friend class CPoolThread;

	size_t max_threads;
	size_t max_waiting_threads;
	std::string idle_name;
};