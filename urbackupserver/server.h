#ifndef URB_SERVER_H
#define URB_SERVER_H

#include <map>
#include "../Interface/Pipe.h"
#include "../Interface/Thread.h"
#include "../Interface/Query.h"
#include "fileclient/FileClient.h"

class IPipeThrottler;
class IMutex;
class IDatabase;

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
	~BackupServer();

	void operator()(void);

	static IPipeThrottler *getGlobalInternetThrottler(size_t speed_bps);
	static IPipeThrottler *getGlobalLocalThrottler(size_t speed_bps);

	static void cleanupThrottlers(void);

	static void testSnapshotAvailability(IDatabase *db);
	static bool isSnapshotsEnabled(void);

	static void testFilesystemTransactionAvailabiliy(IDatabase *db);
	static bool isFilesystemTransactionEnabled();

private:
	void findClients(FileClient &fc);
	void startClients(FileClient &fc);
	void removeAllClients(void);

	std::map<std::wstring, SClient> clients;

	IQuery *q_get_extra_hostnames;
	IQuery *q_update_extra_ip;

	IPipe *exitpipe;

	static IPipeThrottler *global_internet_throttler;
	static IPipeThrottler *global_local_throttler;
	static IMutex *throttle_mutex;

	bool internet_test_mode;

	static bool snapshots_enabled;
	static bool filesystem_transactions_enabled;
};

#endif //URB_SERVER_H
