#include "Interface/Thread.h"
#include "Interface/Mutex.h"
#include "Interface/Condition.h"
#include <deque>
#include <vector>
#include "types.h"

class CClient;
class CWorkerThread;

const size_t max_clients=60;

class CSelectThread : public IThread
{
public:
	CSelectThread(_u32 pWorkerThreadsPerMaster);
	~CSelectThread();

	void operator()();

	bool AddClient(CClient *client);
	bool RemoveClient(CClient *client);

	size_t FreeClients(void);

	void WakeUp(void);
private:
	void FindWorker(CClient *client);

	std::deque<CClient*> clients;

	IMutex *mutex;
	ICondition* cond;
	
	IMutex *stop_mutex;
	ICondition *stop_cond;
	
	bool run;
};
