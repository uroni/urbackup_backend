#include "../Interface/Thread.h"

class BackupServerGet;

class ServerPingThread : public IThread
{
public:
	ServerPingThread(BackupServerGet *pServer_get);
	void operator()(void);
	void setStop(bool b);

private:
	BackupServerGet *server_get;
	volatile bool stop;
};