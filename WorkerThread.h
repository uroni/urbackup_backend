#include "Interface/Thread.h"
#include "Interface/Mutex.h"
#include "Interface/Condition.h"
#include "types.h"
#include "Interface/Types.h"
#include <deque>

class CClient;
class CSelectThread;
class FCGIRequest;

const _u32 WT_BUFFERSIZE=2000;

class CWorkerThread : public IThread
{
public:
	CWorkerThread(CSelectThread *pMaster);
	~CWorkerThread();

	void operator()();
	
	void shutdown(void);

private:
	void ProcessRequest(CClient *client, FCGIRequest *req);
	
	POSTFILE_KEY ParseMultipartData(const std::string &data, const std::string &boundary);

	bool keep_alive;
	bool run;

	CSelectThread* Master;
	
	IMutex* stop_mutex;
	ICondition* stop_cond;
};
