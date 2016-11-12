#ifndef URB_SERVER_H
#define URB_SERVER_H

#include <map>
#include "../Interface/Pipe.h"
#include "../Interface/Thread.h"
#include "../Interface/Query.h"
#include "../urbackupcommon/fileclient/FileClient.h"

class IPipeThrottler;
class IMutex;
class IDatabase;

struct SClient
{
	IPipe *pipe;
	int offlinecount;
	int changecount;
	sockaddr_in addr;
	bool internet_connection;
};

class BackupServer : public IThread
{
public:
	BackupServer(IPipe *pExitpipe);
	~BackupServer();

	void operator()(void);

	static IPipeThrottler *getGlobalInternetThrottler(int speed_bps);
	static IPipeThrottler *getGlobalLocalThrottler(int speed_bps);

	static void cleanupThrottlers(void);

	static void testSnapshotAvailability(IDatabase *db);
	static bool isFileSnapshotsEnabled();
	static bool isImageSnapshotsEnabled();

	enum ESnapshotMethod
	{
		ESnapshotMethod_None = -1,
		ESnapshotMethod_Btrfs = 0,
		ESnapshotMethod_Zfs = 1
	};

	static ESnapshotMethod getSnapshotMethod();

	static void testFilesystemTransactionAvailabiliy(IDatabase *db);
	static bool isFilesystemTransactionEnabled();

	static void updateDeletePending();

	static void forceOfflineClient(const std::string& clientname);

	static void setVirtualClients(const std::string& clientname, const std::string& virtual_clients);

	static bool useTreeHashing();

	static void setupUseTreeHashing();

private:
	void findClients(FileClient &fc);
	void startClients(FileClient &fc);
	void removeAllClients(void);
	void maybeUpdateDeletePendingClients();
	bool isDeletePendingClient(const std::string& clientname);
	void maybeUpdateExistingClientsLower();
	void fixClientnameCase(std::string& clientname);
	static void enableSnapshots(int method);
	void runServerRecovery(IDatabase* db);
	std::string findFile(const std::string& path, const std::string& fn);

	std::map<std::string, SClient> clients;

	bool update_existing_client_names;
	std::vector<std::string> existing_client_names;
	std::vector<std::string> existing_client_names_lower;

	IQuery *q_get_extra_hostnames;
	IQuery *q_update_extra_ip;
	IQuery *q_get_clientnames;
	IQuery *q_update_lastseen;

	IPipe *exitpipe;

	static IPipeThrottler *global_internet_throttler;
	static IPipeThrottler *global_local_throttler;
	static IMutex *throttle_mutex;

	bool internet_only_mode;

	static bool file_snapshots_enabled;
	static bool image_snapshots_enabled;
	static ESnapshotMethod snapshot_method;
	static bool filesystem_transactions_enabled;
	static bool use_tree_hashing;

	static volatile bool update_delete_pending_clients;
	std::vector<std::string> delete_pending_clients;

	static IMutex* force_offline_mutex;
	static std::vector<std::string> force_offline_clients;

	static IMutex* virtual_clients_mutex;
	static std::map<std::string, std::vector<std::string> > virtual_clients;
};

#endif //URB_SERVER_H
