#pragma once

#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include "../urlplugin/IUrlFactory.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../urbackupcommon/os_functions.h"
#include "server_hash.h"
#include "server_prepare_hash.h"
#include "server_status.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "server_settings.h"

#include <memory>
#include <mutex>
#include "server_log.h"

class ServerVHDWriter;
class IFile;
class IPipe;
class ServerPingThread;
class FileClient;
class IPipeThrottler;
class BackupServerContinuous;
class ContinuousBackup;
class Backup;
class ServerBackupDao;
class ImageBackup;
class ServerChannelThread;
class IECDHKeyExchange;
class IECIESDecryption;

const int c_group_default = 0;
const int c_group_continuous = 1;
const int c_group_max = 99;
const int c_group_size = 100;

struct SProtocolVersions
{
	SProtocolVersions() :
			filesrv_protocol_version(0), file_protocol_version(0),
				file_protocol_version_v2(0), set_settings_version(0),
				image_protocol_version(0), eta_version(0), cdp_version(0),
				efi_version(0), file_meta(0), select_sha_version(0),
				client_bitmap_version(0), cmd_version(0),
				symbit_version(0), phash_version(0),
				wtokens_version(0), update_vols(0),
				update_capa_interval(0), require_previous_cbitmap(0),
				async_index_version(0), restore_version(0),
				filesrvtunnel(0)
			{

			}

	int filesrv_protocol_version;
	int file_protocol_version;
	int file_protocol_version_v2;
	int set_settings_version;
	int image_protocol_version;
	int eta_version;
	int cdp_version;
	int efi_version;
	int file_meta;
	int select_sha_version;
	int client_bitmap_version;
	int cmd_version;
	int require_previous_cbitmap;
	int async_index_version;
	int symbit_version;
	int phash_version;
	int wtokens_version;
	int update_vols;
	int update_capa_interval;
	std::string os_simple;
	int restore_version;
	int filesrvtunnel;
};

struct SRunningBackup
{
	SRunningBackup()
		: backup(NULL), ticket(ILLEGAL_THREADPOOL_TICKET),
		group(c_group_default), running(false)
	{

	}


	Backup* backup;
	bool running;
	THREADPOOL_TICKET ticket;
	int group;
	std::string letter;
};

struct SRunningRestore
{
	SRunningRestore(std::string restore_identity, logid_t log_id, int64 status_id, int64 restore_id)
		: restore_identity(restore_identity), log_id(log_id), status_id(status_id), restore_id(restore_id),
		  last_active(Server->getTimeMS())
	{}

	std::string restore_identity;
	logid_t log_id;
	int64 status_id;
	int64 restore_id;
	int64 last_active;
};

struct SRunningLocalBackup
{
	SRunningLocalBackup(logid_t log_id, int64 status_id, int backupid)
		: log_id(log_id), status_id(status_id), backupid(backupid),
		last_active(Server->getTimeMS())
	{}

	logid_t log_id;
	int64 status_id;
	int backupid;
	int64 last_active;
};

struct SShareCleanup
{
	SShareCleanup(std::string name, std::string identity, bool cleanup_file, bool remove_callback)
		: name(name), identity(identity), cleanup_file(cleanup_file), remove_callback(remove_callback)
	{

	}

	std::string name;
	std::string identity;
	bool cleanup_file;
	bool remove_callback;
};

namespace
{
	const int CAPA_NO_IMAGE_BACKUPS = 1;
}

class ClientMain : public IThread, public FileClientChunked::ReconnectionCallback,
	public FileClient::ReconnectionCallback, public INotEnoughSpaceCallback,
	public FileClient::NoFreeSpaceCallback, public FileClientChunked::NoFreeSpaceCallback,
	public FileClient::ProgressLogCallback
{
public:
	ClientMain(IPipe *pPipe, FileClient::SAddrHint pAddr, const std::string &pName, const std::string& pSubName, const std::string& pMainName, int filebackup_group_offset,
		bool internet_connection, bool use_file_snapshots, bool use_image_snapshots, bool use_reflink);
	~ClientMain(void);

	void operator()(void);

	bool authenticateIfNeeded(bool retry_exit, bool force);

	struct SConnection
	{
		SConnection()
			: conn(),
			internet_connection(false)
		{}

		std::unique_ptr<IPipe> conn;
		bool internet_connection;
	};

	bool sendClientMessage(const std::string &msg, const std::string &retok, const std::string &errmsg, unsigned int timeout, bool logerr=true, int max_loglevel=LL_ERROR, bool *retok_err=NULL, std::string* retok_str=NULL, SConnection* conn=NULL, bool do_encrypt = true);
	bool sendClientMessageRetry(const std::string &msg, const std::string &retok, const std::string &errmsg, unsigned int timeout, size_t retry=0, bool logerr=true, int max_loglevel=LL_ERROR, bool *retok_err=NULL, std::string* retok_str=NULL, bool do_encrypt = true);
	std::string sendClientMessage(const std::string &msg, const std::string &errmsg, unsigned int timeout, bool logerr=true, int max_loglevel=LL_ERROR, SConnection* conn=NULL, bool do_encrypt = true);
	std::string sendClientMessageRetry(const std::string &msg, const std::string &errmsg, unsigned int timeout, size_t retry=0, bool logerr=true, int max_loglevel=LL_ERROR, unsigned int timeout_after_first=0, bool do_encrypt=true);
	void sendToPipe(const std::string &msg);

	bool createDirectoryForClient();

	FileClient::SAddrHint getClientaddr(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static MailServer getMailServerSettings(void);
	static bool sendMailToAdmins(const std::string& subj, const std::string& message);

	static int getNumberOfRunningBackups(void);
	static int getNumberOfRunningFileBackups(void);
	static bool tooManyClients(IDatabase *db, const std::string &clientname, ServerSettings *server_settings);
	static int getClientID(IDatabase *db, const std::string &clientname, ServerSettings *server_settings, 
		bool *new_client, std::string* authkey=NULL, int* client_group=NULL, std::string* perm_uid=NULL);

	IPipe *getClientCommandConnection(ServerSettings* server_settings, int timeoutms=10000, std::string* clientaddr=NULL, bool do_encrypt=true);

	virtual IPipe * new_fileclient_connection(void);

	virtual bool handle_not_enough_space(const std::string &path);

	virtual bool handle_not_enough_space(const std::string &path, logid_t logid);

	static IFsFile *getTemporaryFileRetry(bool use_tmpfiles, const std::string& tmpfile_path, logid_t logid);

	static void destroyTemporaryFile(IFile *tmp);

	
	
	virtual void log_progress( const std::string& fn, int64 total, int64 downloaded, int64 speed_bps );

	_u32 getClientFilesrvConnection(FileClient *fc, ServerSettings* server_settings, int timeoutms=10000);

	bool getClientChunkedFilesrvConnection(std::unique_ptr<FileClientChunked>& fc_chunked,
		ServerSettings* server_settings, FileClientChunked::NoFreeSpaceCallback* no_free_space_callback, int timeoutms=10000);

	bool isOnInternetConnection()
	{
		return internet_connection;
	}

	SProtocolVersions getProtocolVersions()
	{
		return protocol_versions;
	}

	void refreshSessionIdentity()
	{
		IScopedLock lock(clientaddr_mutex);
		session_identity_refreshtime = Server->getTimeMS();
	}

	std::string getIdentity();

	std::string getSecretSessionKey();

	std::string getSessionCompression(int& level);
	
	int getCurrImageVersion()
	{
		return curr_image_version;
	}

	static bool run_script(std::string name, const std::string& params, logid_t logid);

	void stopBackupRunning(bool file);

	void updateClientAddress(const std::string& address_data);

	IPipe* getInternalCommandPipe()
	{
		return pipe;
	}

	void setContinuousBackup(BackupServerContinuous* cb);

	void addContinuousChanges( const std::string& changes );

	static void addShareToCleanup(int clientid, const SShareCleanup& cleanupData);

	static void cleanupRestoreShare(int clientid, std::string restore_identity);

	void updateLocalBackup(int64 status_id, bool success, bool should_backoff);

	bool removeRestore(int64 restore_id);

	bool removeLocalBackup(int64 status_id);

	bool updateRestoreRunning(int64 restore_id);

	bool updateLocalBackupRunning(int backupid);

	static bool startBackupBarrier(int64 timeout_seconds);

	static void stopBackupBarrier();

	void updateCapa();

	std::vector<SLogEntry> parseLogData(int64 max_duration, const std::string& data);

	bool isDataplanOkay(ServerSettings* local_settings, bool file);

	static std::string getClientIpStr(const std::string& clientname);

	void setConnectionMetered(bool b);

	void setWindowsLocked(bool b);

	void forceReauthenticate();

	static std::string normalizeVolumeUpper(std::string volume);

	std::vector<std::string> getAllowRestoreClients()
	{
		IScopedLock lock(clientaddr_mutex);
		return allow_restore_clients;
	}

	static void wakeupClientUidReset();

private:
	void unloadSQL(void);
	void prepareSQL(void);
	void updateLastseen(int64 lastseen);
	bool isUpdateFull(int tgroup);
	bool isUpdateIncr(int tgroup);
	bool isUpdateFullImage(void);
	bool isUpdateIncrImage(void);
	bool isUpdateFullImage(const std::string &letter);
	bool isUpdateIncrImage(const std::string &letter);
	void sendClientBackupIncrIntervall(void);
	void sendSettings(void);
	bool getClientSettings(bool& doesnt_exist);
	bool updateClientSetting(const std::string &key, const std::string &value, int use, int64 use_last_modified);
	void sendClientLogdata(void);
	bool isRunningImageBackup(const std::string& letter);
	bool isImageGroupQueued(const std::string& letter, bool full);
	bool isRunningFileBackup(int group, bool queue_only=true);	
	void checkClientVersion(void);
	bool sendFile(IPipe *cc, IFile *f, int timeout);
	bool isBackupsRunningOkay(bool file, bool incr=false);	
	bool updateCapabilities(bool* needs_restart);
	IPipeThrottler *getThrottler(int speed_bps);
	bool inBackupWindow(Backup* backup);
	void updateClientAccessKey();
	bool isDataplanOkay(bool file);
	bool isOnline(ServerChannelThread& channel_thread);

	unsigned int exponentialBackoffTime(size_t count, unsigned int sleeptime, unsigned div);
	bool exponentialBackoff(size_t count, int64 lasttime, unsigned int sleeptime, unsigned div);
	unsigned int exponentialBackoffTimeImage();
	unsigned int exponentialBackoffTimeFile();
	bool exponentialBackoffImage();
	bool exponentialBackoffFile();
	bool exponentialBackoffCdp();
	bool pauseRetryBackup();

	bool sendServerIdentity(bool retry_exit);
	bool authenticatePubKeyInt(IECDHKeyExchange* ecdh_key_exchange);
	bool authenticatePubKey();

	void timeoutRestores();

	void timeoutLocalBackups();

	void cleanupShares();
	static void cleanupShare(SShareCleanup& tocleanup);

	void finishFailedRestore(std::string restore_identity, logid_t log_id, int64 status_id, int64 restore_id);

	void finishLocalBackup(bool success, logid_t log_id, int64 status_id, int backupid);

	bool isLocalBackupRunning(int64 status_id);

	bool isBackupFinished(const SRunningBackup& rb);

	bool renameClient(const std::string& clientuid);
	void updateVirtualClients();

	bool checkClientName(bool& continue_start_backups);


	struct SPathComponents
	{
		std::string backupfolder;
		std::string clientname;
		std::string path;
	};

	SPathComponents extractBackupComponents(const std::string& path, const std::string& backupfolder, const std::vector<std::string>& old_backupfolders);
	std::string curr_image_format;

	IPipe *pipe;
	IDatabase *db;

	FileClient::SAddrHint clientaddr;
	IMutex *clientaddr_mutex;
	std::string clientname;
	std::string clientsubname;
	std::string clientmainname;
	int filebackup_group_offset;

	std::string tmpfile_path;
	static size_t tmpfile_num;
	static IMutex *tmpfile_mutex;

	int clientid;
	std::string perm_uid;

	std::unique_ptr<ServerSettings> server_settings;

	IQuery *q_update_lastseen;
	IQuery *q_update_setting;
	IQuery *q_get_setting;
	IQuery *q_insert_setting;
	IQuery *q_get_unsent_logdata;
	IQuery *q_set_logdata_sent;

	bool can_backup_images;

	bool do_full_backup_now;
	bool do_incr_backup_now;
	bool do_update_settings;
	bool do_full_image_now;
	bool do_incr_image_now;
	bool do_update_access_key;
	bool cdp_needs_sync;

	static int running_backups;
	static int running_file_backups;
	static IMutex *running_backup_mutex;

	SProtocolVersions protocol_versions;
	volatile bool internet_connection;
	volatile bool connection_metered;
	std::atomic<bool> windows_locked;
	int update_version;
	std::string all_volumes;
	std::string all_nonusb_volumes;

	bool use_file_snapshots;
	bool use_image_snapshots;
	bool use_reflink;
	bool use_tmpfiles;
	bool use_tmpfiles_images;

	CTCPStack tcpstack;
	CTCPStack tcpstack_checksum;

	IMutex* throttle_mutex;
	IPipeThrottler *client_throttler;

	int64 last_backup_try;
	
	int64 last_image_backup_try;
	size_t count_image_backup_try;

	int64 last_file_backup_try;
	size_t count_file_backup_try;

	int64 last_cdp_backup_try;
	size_t count_cdp_backup_try;

	std::string session_identity;
	int64 session_identity_refreshtime;
	std::string secret_session_key;
	std::string session_compressed;
	int session_compression_level;

	ServerBackupDao* backup_dao;

	int64 client_updated_time;
	
	BackupServerContinuous* continuous_backup;
	IMutex* continuous_mutex;

	unsigned int curr_image_version;

	std::vector<SRunningBackup> backup_queue;

	static IMutex* cleanup_mutex;
	static std::map<int, std::vector<SShareCleanup> > cleanup_shares;

	logid_t logid;
	static int restore_client_id;
	static bool running_backups_allowed;

	int last_incr_freq;
	int last_startup_backup_delay;

	std::string curr_server_token;
	bool needs_authentification;

	std::unique_ptr<IMutex> restore_mutex;
	std::vector<SRunningRestore> running_restores;

	std::mutex local_backup_mutex;
	std::vector<SRunningLocalBackup> running_local_backups;

	std::vector< std::map<ImageBackup*, bool>  > running_image_groups;

	volatile bool update_capa;

	volatile bool do_reauthenticate;

	std::vector<std::string> allow_restore_clients;

	static IMutex* ecdh_key_exchange_mutex;
	static std::vector<std::pair<IECDHKeyExchange*, int64> > ecdh_key_exchange_buffer;

	static IMutex* client_uid_reset_mutex;
	static ICondition* client_uid_reset_cond;
};
