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

	static void updateDeletePending();

	static void forceOfflineClient(const std::wstring& clientname);

private:
	void findClients(FileClient &fc);
	void startClients(FileClient &fc);
	void removeAllClients(void);
	void maybeUpdateDeletePendingClients();
	bool isDeletePendingClient(const std::wstring& clientname);
	void maybeUpdateExistingClientsLower();
	void fixClientnameCase(std::wstring& clientname);

	std::map<std::wstring, SClient> clients;

	bool update_existing_client_names;
	std::vector<std::wstring> existing_client_names;
	std::vector<std::wstring> existing_client_names_lower;

	IQuery *q_get_extra_hostnames;
	IQuery *q_update_extra_ip;
	IQuery *q_get_clientnames;

	IPipe *exitpipe;

	static IPipeThrottler *global_internet_throttler;
	static IPipeThrottler *global_local_throttler;
	static IMutex *throttle_mutex;

	bool internet_only_mode;

	static bool snapshots_enabled;
	static bool filesystem_transactions_enabled;

	static volatile bool update_delete_pending_clients;
	std::vector<std::wstring> delete_pending_clients;

	static IMutex* force_offline_mutex;
	static std::vector<std::wstring> force_offline_clients;
};

#endif //URB_SERVER_H
