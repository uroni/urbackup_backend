#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"

class ClientMain;

class ServerRunningUpdater : public IThread
{
public:
	ServerRunningUpdater(int pBackupid, bool pImage);
	~ServerRunningUpdater();

	void operator()(void);

	void stop(void);
	void suspend(bool b);

	void setBackupid(int pBackupid);

private:
	volatile bool do_stop;
	bool image;
	int backupid;
	volatile bool suspended;

	IMutex *mutex;
	ICondition *cond;
};
