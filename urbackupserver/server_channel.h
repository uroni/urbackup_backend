#include "../Interface/Thread.h"
#include "../Interface/Pipe.h"
#include "../Interface/Mutex.h"
#include "fileclient/socket_header.h"
#include "../urbackupcommon/fileclient/tcpstack.h"

class BackupServerGet;

class ServerSettings;

class ServerChannelThread : public IThread
{
public:
	ServerChannelThread(BackupServerGet *pServer_get, int clientid);
	~ServerChannelThread(void);

	void operator()(void);

	std::string processMsg(const std::string &msg);

	void doExit(void);

private:
	unsigned int lasttime;
	int clientid;

	int constructCapabilities(void);

	BackupServerGet *server_get;
	IPipe *exitpipe;
	IPipe *input;
	CTCPStack tcpstack;

	ServerSettings *settings;

	IMutex *mutex;

	volatile bool do_exit;
	bool combat_mode;
};