#include "../Interface/Service.h"
#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../cryptoplugin/CryptoFactory.h"

#include <map>
#include <deque>

class ClientService : public IService
{
public:
	virtual ICustomClient* createClient();
	virtual void destroyClient( ICustomClient * pClient);
};

enum ClientConnectorState
{
	CCSTATE_NORMAL=0,
	CCSTATE_START_FILEBACKUP=1,
	CCSTATE_SHADOWCOPY=2,
	CCSTATE_CHANNEL=3,
	CCSTATE_IMAGE=4,
	CCSTATE_IMAGE_HASHDATA=5,
	CCSTATE_UPDATE_DATA=6,
	CCSTATE_UPDATE_FINISH=7,
	CCSTATE_STATUS=9,
	CCSTATE_FILESERV=10,
	CCSTATE_IMAGE_BITMAP=11,
	CCSTATE_START_FILEBACKUP_ASYNC=12
};

enum ThreadAction
{
	TA_NONE=0,
	TA_FULL_IMAGE=1,
	TA_INCR_IMAGE=2
};

enum RunningAction
{
	RUNNING_NONE=0,
	RUNNING_INCR_FILE=1,
	RUNNING_FULL_FILE=2,
	RUNNING_FULL_IMAGE=3,
	RUNNING_INCR_IMAGE=4,
	RUNNING_RESUME_INCR_FILE=5,
	RUNNING_RESUME_FULL_FILE=6,
	RUNNING_RESTORE_FILE=8,
	RUNNING_RESTORE_IMAGE=9
};

const int c_use_group = 1;
const int c_use_value = 2;
const int c_use_value_client = 4;

struct SRunningProcess
{
	SRunningProcess()
		: id(0), action(RUNNING_NONE),
		pcdone(-1), eta_ms(-1),
		server_id(0),
		last_pingtime(Server->getTimeMS()),
		detail_pc(-1),
		total_bytes(-1),
		done_bytes(0),
		speed_bpms(0),
		refs(0)
	{}

	int64 id;
	int64 server_id;
	RunningAction action;
	int pcdone;
	int64 eta_ms;
	std::string server_token;
	int64 last_pingtime;
	std::string details;
	int detail_pc;
	int64 total_bytes;
	int64 done_bytes;
	double speed_bpms;
	size_t refs;
};

struct SFinishedProcess
{
	SFinishedProcess()
		:id(0), success(false)
	{
	}

	SFinishedProcess(int64 id, bool success)
		: id(id), success(success)
	{}

	int64 id;
	bool success;
};

enum RestoreOkStatus
{
	RestoreOk_None,
	RestoreOk_Wait,
	RestoreOk_Declined,
	RestoreOk_Ok
};

class ImageThread;

struct ImageInformation
{
	ThreadAction thread_action;
	THREADPOOL_TICKET thread_ticket;
	std::string shadowdrive;
	int64 startpos;
	int shadow_id;
	std::string image_letter;
	std::string orig_image_letter;
	bool no_shadowcopy;
	ImageThread *image_thread;
	bool with_checksum;
	bool with_bitmap;
	std::string clientsubname;
	int64 running_process_id;
	int64 server_status_id;
};

struct SRestoreToken
{
	SRestoreToken()
		: process_id(0), restore_token_time()
	{}

	int64 process_id;
	std::string restore_token;
	int64 restore_token_time;
};

struct SChannel
{
	SChannel(IPipe *pipe, bool internet_connection, std::string endpoint_name,
		std::string token, bool* make_fileserv, std::string server_identity,
		int capa, int restore_version, std::string virtual_client)
		: pipe(pipe), internet_connection(internet_connection), endpoint_name(endpoint_name),
		  token(token), make_fileserv(make_fileserv), server_identity(server_identity),
		state(EChannelState_Idle), capa(capa), restore_version(restore_version),
		virtual_client(virtual_client) {}
	SChannel(void)
		: pipe(NULL), internet_connection(false), make_fileserv(NULL),
		state(EChannelState_Idle), capa(0), restore_version(0) {}

	IPipe *pipe;
	bool internet_connection;
	std::string endpoint_name;
	std::string token;
	bool* make_fileserv;
	std::string last_tokens;
	std::string server_identity;
	int restore_version;
	std::string virtual_client;

	enum EChannelState
	{
		EChannelState_Idle,
		EChannelState_Pinging,
		EChannelState_Exit,
		EChannelState_Used
	};

	EChannelState state;
	int capa;
};

struct SAsyncFileList
{
	int64 last_update;
	unsigned int result_id;
	size_t refcount;
};

struct SVolumesCache;
class RestoreFiles;

const unsigned int x_pingtimeout=180000;

class ClientConnector : public ICustomClient
{
	friend class ScopedRemoveRunningBackup;
public:
	ClientConnector(void);
	virtual void Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName);
	~ClientConnector(void);

	virtual bool Run(IRunOtherCallback* run_other);
	virtual void ReceivePackets(IRunOtherCallback* run_other);

	static void init_mutex(void);
	static void destroy_mutex(void);

	virtual bool wantReceive(void);

	virtual bool closeSocket( void );

	static int64 getLastTokenTime(const std::string & tok);

	void doQuitClient(void);
	bool isQuitting(void);
	bool isHashdataOkay(void);

	void setIsInternetConnection(void);

	static bool isBackupRunning();

	static bool tochannelSendChanges(const char* changes, size_t changes_size);

	static bool tochannelLog(int64 log_id, const std::string& msg, int loglevel, const std::string& identity);

	static void updateRestorePc(int64 local_process_id, int64 restore_id, int64 status_id, int nv, const std::string& identity,
		const std::string& fn, int fn_pc, int64 total_bytes, int64 done_bytes, double speed_bpms);

	static bool restoreDone(int64 log_id, int64 status_id, int64 restore_id, bool success, const std::string& identity);

	static IPipe* getFileServConnection(const std::string& server_token, unsigned int timeoutms);

	static void requestRestoreRestart();

	static void updateLastBackup(void);

	static int64 addNewProcess(SRunningProcess proc);
	static bool updateRunningPc(int64 id, int pcdone);
	static bool removeRunningProcess(int64 id, bool success, bool consider_refs=false);

	static void timeoutFilesrvConnections();

	static void timeoutBackupImmediate(int64 start_timeout, int64 resume_timeout, RunningAction ra, bool& has_backup, int64& starttime);

	static std::string removeIllegalCharsFromBackupName(std::string in);

	static bool updateDefaultDirsSetting(IDatabase *db, bool all_virtual_clients, int group_offset, bool update_use);

private:
	bool checkPassword(const std::string &cmd, bool& change_pw);
	bool saveBackupDirs(str_map &args, bool server_default, int group_offset);
	std::string replaceChars(std::string in);
	void updateSettings(const std::string &pData);
	void replaceSettings(const std::string &pData);
	void saveLogdata(const std::string &created, const std::string &pData);
	std::string getLogpoints(void);
	void getLogLevel(int logid, int loglevel, std::string &data);
	bool waitForThread(void);
	bool sendFullImage(void);
	bool sendIncrImage(void);
	bool sendMBR(std::string dl, std::string &errmsg);
    std::string receivePacket(const SChannel& channel, int64 timeoutms = 60000);
	void downloadImage(str_map params, IScopedLock& backup_mutex_lock);
	void removeChannelpipe(IPipe *cp);
	void waitForPings(IScopedLock *lock);
	bool hasChannelPing();
	bool writeUpdateFile(IFile *datafile, std::string outfn);
	std::string getSha512Hash(IFile *fn);
	bool checkHash(std::string shah);
	bool checkVersion(IFile* updatef);
	void tochannelSendStartbackup(RunningAction backup_type, const std::string& virtual_client);
	void ImageErr(const std::string &msg);
	void update_silent(void);
	bool calculateFilehashesOnClient(const std::string& clientsubname);
	void sendStatus();
    bool sendChannelPacket(const SChannel& channel, const std::string& msg);
	bool versionNeedsUpdate(const std::string& local_version, const std::string& server_version);
	int parseVersion(const std::string& version, std::vector<std::string>& features);

	std::string getAccessTokensParams(const std::string& tokens, bool with_clientname, const std::string& virtual_client);

	static bool sendMessageToChannel(const std::string& msg, int timeoutms, const std::string& identity);

	static int64 getLastBackupTime();

	static std::string getHasNoRecentBackup();

	static std::string getCurrRunningJob(bool reset_done, int& pcdone);

	SChannel* getCurrChannel();

	void CMD_ADD_IDENTITY(const std::string &identity, const std::string &cmd, bool ident_ok);
	void CMD_ADD_IDENTITY(const std::string &params);
	void CMD_GET_CHALLENGE(const std::string &identity, const std::string& cmd);
	void CMD_SIGNATURE(const std::string &identity, const std::string &cmd);
	void CMD_START_INCR_FILEBACKUP(const std::string &cmd);
	void CMD_START_FULL_FILEBACKUP(const std::string &cmd);
	void CMD_WAIT_FOR_INDEX(const std::string &cmd);
	void CMD_START_SHADOWCOPY(const std::string &cmd);
	void CMD_STOP_SHADOWCOPY(const std::string &cmd);
	void CMD_SET_INCRINTERVAL(const std::string &cmd);
	void CMD_GET_BACKUPDIRS(const std::string &cmd);
	void CMD_SAVE_BACKUPDIRS(const std::string &cmd, str_map &params);
	void CMD_DID_BACKUP(const std::string &cmd);
	void CMD_DID_BACKUP2(const std::string &cmd);
	void CMD_BACKUP_FAILED(const std::string& cmd);
	void CMD_STATUS(const std::string &cmd);
	void CMD_STATUS_DETAIL(const std::string &cmd);
	void CMD_UPDATE_SETTINGS(const std::string &cmd);
	void CMD_PING_RUNNING(const std::string &cmd);
	void CMD_PING_RUNNING2(const std::string &cmd);
	void CMD_CHANNEL(const std::string &cmd, IScopedLock *g_lock, const std::string& identity);
	void CMD_CHANNEL_PONG(const std::string &cmd, const std::string& endpoint_name);
	void CMD_CHANNEL_PING(const std::string &cmd, const std::string& endpoint_name);
	void CMD_TOCHANNEL_START_INCR_FILEBACKUP(const std::string &cmd, str_map &params);
	void CMD_TOCHANNEL_START_FULL_FILEBACKUP(const std::string &cmd, str_map &params);
	void CMD_TOCHANNEL_START_FULL_IMAGEBACKUP(const std::string &cmd, str_map &params);
	void CMD_TOCHANNEL_START_INCR_IMAGEBACKUP(const std::string &cmd, str_map &params);
	void CMD_TOCHANNEL_UPDATE_SETTINGS(const std::string &cmd);
	void CMD_LOGDATA(const std::string &cmd);
	void CMD_PAUSE(const std::string &cmd);
	void CMD_GET_LOGPOINTS(const std::string &cmd);
	void CMD_GET_LOGDATA(const std::string &cmd, str_map &params);
	void CMD_FULL_IMAGE(const std::string &cmd, bool ident_ok);
	void CMD_INCR_IMAGE(const std::string &cmd, bool ident_ok);
	void CMD_MBR(const std::string &cmd);
	void CMD_RESTORE_GET_BACKUPCLIENTS(const std::string &cmd);
	void CMD_RESTORE_GET_BACKUPIMAGES(const std::string &cmd);
	void CMD_RESTORE_GET_FILE_BACKUPS(const std::string &cmd);
	void CMD_RESTORE_GET_FILE_BACKUPS_TOKENS(const std::string &cmd, str_map &params);
	void CMD_GET_FILE_LIST_TOKENS(const std::string &cmd, str_map &params);
	void CMD_DOWNLOAD_FILES_TOKENS(const std::string &cmd, str_map &params);
	void CMD_RESTORE_DOWNLOAD_IMAGE(const std::string &cmd, str_map &params);
	void CMD_RESTORE_DOWNLOAD_FILES(const std::string &cmd, str_map &params);
	void CMD_RESTORE_DOWNLOADPROGRESS(const std::string &cmd);
	void CMD_RESTORE_LOGIN_FOR_DOWNLOAD(const std::string &cmd, str_map &params);
	void CMD_RESTORE_GET_SALT(const std::string &cmd, str_map &params);
	void CMD_VERSION_UPDATE(const std::string &cmd);
	void CMD_CLIENT_UPDATE(const std::string &cmd);
	void CMD_CAPA(const std::string &cmd);
	void CMD_NEW_SERVER(str_map &params);
	void CMD_RESET_KEEP(str_map &params);
	void CMD_ENABLE_END_TO_END_FILE_BACKUP_VERIFICATION(const std::string &cmd);
	void CMD_GET_VSSLOG(const std::string &cmd);
	void CMD_GET_ACCESS_PARAMS(str_map &params);
	void CMD_CONTINUOUS_WATCH_START();
	void CMD_SCRIPT_STDERR(const std::string& cmd);
	void CMD_FILE_RESTORE(const std::string& cmd);
	void CMD_RESTORE_OK(str_map &params);
	void CMD_CLIENT_ACCESS_KEY(const std::string& cmd);
	void CMD_WRITE_TOKENS(const std::string& cmd);
	void CMD_GET_CLIENTNAME(const std::string& cmd);

	int getCapabilities(IDatabase* db);
	bool multipleChannelServers();
	void exit_backup_immediate(int rc);

	void refreshSessionFromChannel(const std::string& endpoint_name);

	static void timeoutAsyncFileIndex();

	static SRunningProcess* getRunningProcess(RunningAction action, std::string server_token);
	static SRunningProcess* getRunningFileBackupProcess( std::string server_token, int64 server_id);
	static SRunningProcess* getRunningBackupProcess(std::string server_token, int64 server_id);
	static SRunningProcess* getRunningProcess(int64 id);
	static SRunningProcess* getActiveProcess(int64 timeout);
	static std::string actionToStr(RunningAction action);
	static void removeTimedOutProcesses(std::string server_token, bool file);

	unsigned int curr_result_id;
	IPipe *pipe;
	THREAD_ID tid;
	ClientConnectorState state;
	int64 lasttime;
	int64 last_update_time;
	int file_version;
	CTCPStack tcpstack;
	volatile bool do_quit;
	bool is_channel;
	int64 local_backup_running_id;
	bool retrieved_has_components;
	bool status_has_components;

	static std::vector<SRunningProcess> running_processes;
	static std::vector<SFinishedProcess> finished_processes;
	static int64 curr_backup_running_id;
	static IMutex *backup_mutex;
	static IMutex *process_mutex;
	static std::map<std::string, SAsyncFileList> async_file_index;
	static std::deque<std::pair<std::string, std::string> > finished_async_file_index;
	static int backup_interval;
	static int backup_alert_delay;
	static std::vector<SChannel> channel_pipes;
	int64 last_channel_ping;
	static db_results cached_status;
	static std::map<std::string, int64> last_token_times;
	static int last_capa;
	static IMutex *ident_mutex;
	static std::vector<std::string> new_server_idents;
	static bool end_to_end_file_backup_verification_enabled;

	struct SChallenge
	{
		SChallenge()
			: shared_key_exchange(NULL),
			local_compressed(false)
		{
		}

		SChallenge(std::string challenge_str,
			IECDHKeyExchange* shared_key_exchange,
			bool local_compressed)
			: challenge_str(challenge_str),
			shared_key_exchange(shared_key_exchange),
			local_compressed(local_compressed)
		{}

		std::string challenge_str;
		IECDHKeyExchange* shared_key_exchange;
		bool local_compressed;
	};

	static std::map < std::pair<std::string, std::string>, SChallenge > challenges;
	static bool has_file_changes;
	static bool last_metered;

	struct SFilesrvConnection
	{
		SFilesrvConnection()
			: starttime(Server->getTimeMS()), pipe(NULL)
		{}

		SFilesrvConnection(std::string token, IPipe* pipe)
			: token(token), starttime(Server->getTimeMS()), pipe(pipe)
		{}

		std::string token;
		int64 starttime;
		IPipe* pipe;
	};

	static std::vector<SFilesrvConnection> fileserv_connections;
	static RestoreOkStatus restore_ok_status;
	static RestoreFiles* restore_files;
	static bool status_updated;
	static size_t needs_restore_restart;
	static size_t ask_restore_ok;
	static int64 service_starttime;
	static SRestoreToken restore_token;

	IFile* hashdatafile;
	unsigned int hashdataleft;
	IFile* bitmapfile;
	unsigned int bitmapleft;
	volatile bool hashdataok;
	bool silent_update;

	ImageInformation image_inf;

	std::string server_token;

	bool want_receive;

	bool internet_conn;

	std::string endpoint_name;

	bool make_fileserv;

#ifdef _WIN32
	static SVolumesCache* volumes_cache;
#endif
	IRunOtherCallback* run_other;

	int64 idle_timeout;

	static int64 startup_timestamp;

	std::string async_file_list_id;

	bool is_encrypted;
};

class ScopedRemoveRunningBackup
{
public:
	ScopedRemoveRunningBackup(int64 id)
		: id(id), success(false)
	{}

	void setSuccess(bool b)
	{
		success = b;
	}

	~ScopedRemoveRunningBackup()
	{
		ClientConnector::removeRunningProcess(id, success);
	}

private:
	int64 id;
	bool success;
};