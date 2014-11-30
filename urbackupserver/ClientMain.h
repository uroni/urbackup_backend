#pragma once

#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include "../urlplugin/IUrlFactory.h"
#include "fileclient/FileClient.h"
#include "fileclient/FileClientChunked.h"
#include "../urbackupcommon/os_functions.h"
#include "server_hash.h"
#include "server_prepare_hash.h"
#include "server_status.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "server_settings.h"

#include <memory>

class ServerVHDWriter;
class IFile;
class IPipe;
class ServerPingThread;
class FileClient;
class IPipeThrottler;
class ServerHashExisting;
class BackupServerContinuous;
class ContinuousBackup;
class Backup;

const int c_group_all = -1;
const int c_group_default = 0;
const int c_group_continuous = 1;

struct SProtocolVersions
{
	SProtocolVersions() :
			filesrv_protocol_version(0), file_protocol_version(0),
				file_protocol_version_v2(0), set_settings_version(0),
				image_protocol_version(0), eta_version(0), cdp_version(0)
			{

			}

	int filesrv_protocol_version;
	int file_protocol_version;
	int file_protocol_version_v2;
	int set_settings_version;
	int image_protocol_version;
	int eta_version;
	int cdp_version;
};

struct SRunningBackup
{
	SRunningBackup()
		: backup(NULL), ticket(ILLEGAL_THREADPOOL_TICKET),
		group(c_group_default)
	{

	}


	Backup* backup;
	THREADPOOL_TICKET ticket;
	int group;
	std::string letter;
};

class ClientMain : public IThread, public FileClientChunked::ReconnectionCallback,
	public FileClient::ReconnectionCallback, public INotEnoughSpaceCallback,
	public FileClient::NoFreeSpaceCallback, public FileClientChunked::NoFreeSpaceCallback,
	public FileClient::ProgressLogCallback
{
	friend class ServerHashExisting;
public:
	ClientMain(IPipe *pPipe, sockaddr_in pAddr, const std::wstring &pName, bool internet_connection, bool use_snapshots, bool use_reflink);
	~ClientMain(void);

	void operator()(void);

	bool sendClientMessage(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, bool logerr=true, int max_loglevel=LL_ERROR, bool *retok_err=NULL, std::string* retok_str=NULL);
	bool sendClientMessageRetry(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, size_t retry=0, bool logerr=true, int max_loglevel=LL_ERROR, bool *retok_err=NULL, std::string* retok_str=NULL);
	std::string sendClientMessage(const std::string &msg, const std::wstring &errmsg, unsigned int timeout, bool logerr=true, int max_loglevel=LL_ERROR);
	std::string sendClientMessageRetry(const std::string &msg, const std::wstring &errmsg, unsigned int timeout, size_t retry=0, bool logerr=true, int max_loglevel=LL_ERROR);
	void sendToPipe(const std::string &msg);
	int getPCDone(void);
	int64 getETAms();
	bool createDirectoryForClient();

	sockaddr_in getClientaddr(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static MailServer getMailServerSettings(void);
	static bool sendMailToAdmins(const std::string& subj, const std::string& message);

	static int getNumberOfRunningBackups(void);
	static int getNumberOfRunningFileBackups(void);
	static int getClientID(IDatabase *db, const std::wstring &clientname, ServerSettings *server_settings, bool *new_client);

	IPipe *getClientCommandConnection(int timeoutms=10000, std::string* clientaddr=NULL);

	virtual IPipe * new_fileclient_connection(void);

	virtual bool handle_not_enough_space(const std::wstring &path);

	static IFile *getTemporaryFileRetry(bool use_tmpfiles, const std::wstring& tmpfile_path, int clientid);

	static void destroyTemporaryFile(IFile *tmp);

	
	
	virtual void log_progress( const std::string& fn, int64 total, int64 downloaded, int64 speed_bps );

	_u32 getClientFilesrvConnection(FileClient *fc, ServerSettings* server_settings, int timeoutms=10000);

	bool getClientChunkedFilesrvConnection(std::auto_ptr<FileClientChunked>& fc_chunked, ServerSettings* server_settings, int timeoutms=10000);

	bool isOnInternetConnection()
	{
		return internet_connection;
	}

	SProtocolVersions getProtocolVersions()
	{
		return protocol_versions;
	}

	std::string getSessionIdentity()
	{
		return session_identity;
	}
	
	int getCurrImageVersion()
	{
		return curr_image_version;
	}

	static void run_script(std::wstring name, const std::wstring& params, int clientid);

	void startBackupRunning(bool file);
	void stopBackupRunning(bool file);

	void updateClientAddress(const std::string& address_data, bool& switch_to_internet_connection);

	IPipe* getInternalCommandPipe()
	{
		return pipe;
	}

	void setContinuousBackup(BackupServerContinuous* cb);

	void addContinuousChanges( const std::string& changes );

private:
	void unloadSQL(void);
	void prepareSQL(void);
	void updateLastseen(void);
	bool isUpdateFull(void);
	bool isUpdateIncr(void);
	bool isUpdateFullImage(void);
	bool isUpdateIncrImage(void);
	bool isUpdateFullImage(const std::string &letter);
	bool isUpdateIncrImage(const std::string &letter);
	void sendClientBackupIncrIntervall(void);
	void sendSettings(void);
	bool getClientSettings(bool& doesnt_exist);
	bool updateClientSetting(const std::wstring &key, const std::wstring &value);
	void sendClientLogdata(void);
	bool isRunningImageBackup(const std::string& letter);
	bool isRunningFileBackup(int group);	
	void checkClientVersion(void);
	bool sendFile(IPipe *cc, IFile *f, int timeout);
	bool isBackupsRunningOkay(bool incr, bool file);	
	bool updateCapabilities(void);
	IPipeThrottler *getThrottler(size_t speed_bps);
	void update_sql_intervals(bool update_sql);

	unsigned int exponentialBackoffTime(size_t count, unsigned int sleeptime, unsigned div);
	bool exponentialBackoff(size_t count, int64 lasttime, unsigned int sleeptime, unsigned div);
	unsigned int exponentialBackoffTimeImage();
	unsigned int exponentialBackoffTimeFile();
	bool exponentialBackoffImage();
	bool exponentialBackoffFile();
	bool exponentialBackoffCdp();

	bool authenticatePubKey();

	
	SSettings curr_intervals;
	std::string curr_image_format;

	IPipe *pipe;
	IDatabase *db;

	sockaddr_in clientaddr;
	IMutex *clientaddr_mutex;
	std::wstring clientname;

	std::wstring tmpfile_path;
	static size_t tmpfile_num;
	static IMutex *tmpfile_mutex;

	int clientid;
	int backupid;

	ISettingsReader *settings;
	ISettingsReader *settings_client;
	ServerSettings *server_settings;

	IQuery *q_update_lastseen;
	IQuery *q_update_full;
	IQuery *q_update_incr;
	IQuery *q_update_setting;
	IQuery *q_insert_setting;
	IQuery *q_set_complete;
	IQuery *q_update_image_full;
	IQuery *q_update_image_incr;
	IQuery *q_get_unsent_logdata;
	IQuery *q_set_logdata_sent;

	SStatus status;

	bool can_backup_images;

	bool do_full_backup_now;
	bool do_incr_backup_now;
	bool do_update_settings;
	bool do_full_image_now;
	bool do_incr_image_now;
	bool cdp_needs_sync;

	static int running_backups;
	static int running_file_backups;
	static IMutex *running_backup_mutex;

	SProtocolVersions protocol_versions;
	volatile bool internet_connection;
	int update_version;
	std::string all_volumes;
	std::string all_nonusb_volumes;

	bool use_snapshots;
	bool use_reflink;
	bool use_tmpfiles;
	bool use_tmpfiles_images;

	CTCPStack tcpstack;

	IMutex* throttle_mutex;
	IPipeThrottler *client_throttler;
	
	int64 last_image_backup_try;
	size_t count_image_backup_try;

	int64 last_file_backup_try;
	size_t count_file_backup_try;

	int64 last_cdp_backup_try;
	size_t count_cdp_backup_try;

	std::string session_identity;

	ServerBackupDao* backup_dao;

	int64 client_updated_time;
	
	BackupServerContinuous* continuous_backup;
	IMutex* continuous_mutex;

	unsigned int curr_image_version;

	std::vector<SRunningBackup> backup_queue;
};
