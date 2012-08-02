#include "../Interface/Thread.h"

class BackupServerGet;

class ServerRunningUpdater : public IThread
{
public:
	ServerRunningUpdater(int pBackupid, bool pImage);

	void operator()(void);

	void stop(void);
	void suspend(bool b);

private:
	volatile bool do_stop;
	bool image;
	int backupid;
	volatile bool suspended;
};