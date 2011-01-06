#ifndef URB_SERVER_H
#define URB_SERVER_H

#include <map>
#include "../Interface/Pipe.h"
#include "../Interface/Thread.h"
#include "fileclient/FileClient.h"

struct SClient
{
	IPipe *pipe;
	int offlinecount;
	sockaddr_in addr;
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

	std::map<std::string, SClient> clients;

	std::wstring backupfolder;
	std::wstring backupfolder_uncompr;

	IPipe *exitpipe;
};

#endif //URB_SERVER_H