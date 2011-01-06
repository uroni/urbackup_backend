#include "../Interface/Thread.h"
#include "../Interface/Pipe.h"
#include "../Interface/Mutex.h"
#include "fileclient/socket_header.h"
#include "fileclient/tcpstack.h"

class BackupServerGet;

class ServerChannelThread : public IThread
{
public:
	ServerChannelThread(BackupServerGet *pServer_get, sockaddr_in pClientaddr);

	void operator()(void);

	std::string processMsg(const std::string &msg);

	void doExit(void);

private:
	unsigned int lasttime;

	BackupServerGet *server_get;
	sockaddr_in clientaddr;
	IPipe *exitpipe;
	IPipe *input;
	CTCPStack tcpstack;

	IMutex *mutex;

	volatile bool do_exit;
};