#ifndef URB_SERVER_H
#define URB_SERVER_H

#include <map>
#include "../Interface/Pipe.h"
#include "../Interface/Thread.h"
#include "../Interface/Query.h"
#include "fileclient/FileClient.h"

struct SClient
{
	IPipe *pipe;
	int offlinecount;
	sockaddr_in addr;
	bool internet_connection;
};

class BackupServer : public IThread
{
public:
	BackupServer(IPipe *pExitpipe);

	void operator()(void);

private:
	void findClients(FileClient &fc);
	void startClients(FileClient &fc);
	void removeAllClients(void);

	std::map<std::wstring, SClient> clients;

	IQuery *q_get_extra_hostnames;
	IQuery *q_update_extra_ip;

	IPipe *exitpipe;
};

#endif //URB_SERVER_H