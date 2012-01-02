#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"

class BackupServerGet;

class ServerPingThread : public IThread
{
public:
	ServerPingThread(BackupServerGet *pServer_get);
	void operator()(void);
	void setStop(bool b);

	bool isTimeout(void);

private:
	BackupServerGet *server_get;
	volatile bool stop;
	volatile bool is_timeout;
};