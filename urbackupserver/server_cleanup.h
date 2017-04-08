#include "../Interface/Database.h"
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include <vector>
#include "dao/ServerCleanupDao.h"
#include "dao/ServerBackupDao.h"
#include "dao/ServerFilesDao.h"
#include <sstream>
#include <memory>
#include <set>
#include "FileIndex.h"
#include "server_log.h"

class ServerSettings;

enum ECleanupAction
{
	ECleanupAction_None,
	ECleanupAction_FreeMinspace,
	ECleanupAction_DeleteFilebackup,
	ECleanupAction_DeleteImagebackup,
	ECleanupAction_RemoveUnknown
};

struct CleanupAction
{
	//None
	CleanupAction(void)
		: action(ECleanupAction_None)
	{
	}

	//Delete file backup
	CleanupAction(std::string backupfolder, int clientid, int backupid, bool force_remove, bool *result)
		: action(ECleanupAction_DeleteFilebackup), backupfolder(backupfolder), clientid(clientid), backupid(backupid),
		  force_remove(force_remove), result(result)
	{

	}

	//Delete image backup
	CleanupAction(int backupid, bool force_remove, bool *result)
		: action(ECleanupAction_DeleteImagebackup), clientid(0), backupid(backupid),
		force_remove(force_remove), result(result)
	{

	}

	//Free minspace
	CleanupAction(int64 minspace, bool *result, bool cleanup_other)
		: action(ECleanupAction_FreeMinspace), minspace(minspace), result(result), cleanup_other(cleanup_other)
	{
	}

	//Remove unknown data
	CleanupAction(ECleanupAction action)
		: action(action)
	{
	}

	ECleanupAction action;
	
	std::string backupfolder;
	int clientid;
	int backupid;
	bool force_remove;
	bool cleanup_other;

	int64 minspace;
	bool *result;
};


class ServerCleanupThread : public IThread
{
public:
	ServerCleanupThread(CleanupAction action);
	~ServerCleanupThread(void);

	void operator()(void);

	static bool cleanupSpace(int64 minspace, bool do_cleanup_other=false);

	static void removeUnknown(void);

	static void updateStats(bool interruptible);

	static bool isUpdateingStats();

	static void disableUpdateStats();

	static void enableUpdateStats();

	static void initMutex(void);
	static void destroyMutex(void);

	static void doQuit(void);

	static void lockImageFromCleanup(int backupid);
	static void unlockImageFromCleanup(int backupid);
	static bool isImageLockedFromCleanup(int backupid);

	static bool isClientlistDeletionAllowed();

	static bool deleteImage(logid_t logid, std::string clientname, std::string path);

	static bool findUncompleteImageRef(ServerCleanupDao* cleanupdao, int backupid);

	static bool findLockedImageRef(ServerCleanupDao* cleanupdao, int backupid);

	static bool findArchivedImageRef(ServerCleanupDao* cleanupdao, int backupid);
private:

	void do_cleanup(void);
	bool do_cleanup(int64 minspace, bool do_cleanup_other=false);

	void do_remove_unknown(void);

	bool correct_target(const std::string& backupfolder, std::string& target);

	bool correct_poolname(const std::string& backupfolder, const std::string& clientname, const std::string& pool_name, std::string& pool_path);

	void check_symlinks(const ServerCleanupDao::SClientInfo& client_info, const std::string& backupfolder);

	int max_removable_incr_images(ServerSettings& settings, int backupid);

	bool cleanup_images_client(int clientid, int64 minspace, std::vector<int> &imageids, bool cleanup_only_one);

	void cleanup_images(int64 minspace=-1);

	void cleanup_files(int64 minspace=-1);

	bool cleanup_one_filebackup_client(int clientid, int64 minspace, int& filebid);

	void cleanup_other();

	void rewrite_history(const std::string& back_start, const std::string& back_stop, const std::string& date_grouping);

	bool cleanup_clientlists();

	void cleanup_client_hist();

	void cleanup_all_system_images(ServerSettings& settings);

	void cleanup_system_images(int clientid, std::string clientname, ServerSettings& settings);

	size_t getImagesFullNum(int clientid, int &backupid_top, const std::vector<int> &notit);
	size_t getImagesIncrNum(int clientid, int &backupid_top, const std::vector<int> &notit);

	size_t getFilesFullNum(int clientid, int &backupid_top);
	size_t getFilesIncrNum(int clientid, int &backupid_top);

	bool removeImage(int backupid, ServerSettings* settings, bool update_stat, bool force_remove, bool remove_associated, bool remove_references);

	void removeClient(int clientid);

	bool deleteFileBackup(const std::string &backupfolder, int clientid, int backupid, bool force_remove=false);

	void removeFileBackupSql( int backupid );

	void deletePendingClients(void);

	bool backup_database(void);

	static bool deleteAndTruncateFile(logid_t logid, std::string path);
	
	int64 getImageSize(int backupid);

	int hasEnoughFreeSpace(int64 minspace, ServerSettings *settings);

	bool truncate_files_recursive(std::string path);

	void enforce_quotas(void);

	bool enforce_quota(int clientid, std::ostringstream& log);

	void delete_incomplete_file_backups();
	void delete_pending_file_backups();
	bool backup_clientlists();
	bool backup_ident();
	void ren_files_backupfolder();

	static void setClientlistDeletionAllowed(bool b);

	IDatabase *db;

	static IMutex *mutex;
	static ICondition *cond;

	static IMutex *a_mutex;

	static bool update_stats;
	static bool update_stats_interruptible;
	static bool update_stats_disabled;
	
	std::vector<int> removeerr;

	static volatile bool do_quit;

	CleanupAction cleanup_action;

	std::auto_ptr<ServerCleanupDao> cleanupdao;
	std::auto_ptr<ServerBackupDao> backupdao;
	std::auto_ptr<ServerFilesDao> filesdao;
	std::auto_ptr<FileIndex> fileindex;

	logid_t logid;

	static IMutex* cleanup_lock_mutex;
	static std::map<int, size_t> locked_images;

	static bool allow_clientlist_deletion;
};

class ScopedLockImageFromCleanup
{
public:
	ScopedLockImageFromCleanup(int backupid)
		: backupid(backupid)
	{
		if (backupid != 0)
			ServerCleanupThread::lockImageFromCleanup(backupid);
	}

	~ScopedLockImageFromCleanup()
	{
		if (backupid != 0)
			ServerCleanupThread::unlockImageFromCleanup(backupid);
	}

	void reset(int nid=0)
	{
		if (backupid != 0)
			ServerCleanupThread::unlockImageFromCleanup(backupid);

		backupid = nid;

		if(backupid!=0)
			ServerCleanupThread::lockImageFromCleanup(backupid);
	}

private:
	int backupid;
};
