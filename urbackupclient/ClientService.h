#include "../Interface/Service.h"
#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../urbackupcommon/fileclient/tcpstack.h"

#include <map>

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
	CCSTATE_WAIT_FOR_CONTRACTORS=8,
	CCSTATE_STATUS=9,
	CCSTATE_FILESERV=10
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
	RUNNING_RESTORE_FILE=8
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
	uint64 startpos;
	int shadow_id;
	std::string image_letter;
	bool no_shadowcopy;
	ImageThread *image_thread;
	bool with_checksum;
	bool with_emptyblocks;
	bool with_bitmap;
};

struct SChannel
{
	SChannel(IPipe *pipe, bool internet_connection, std::string endpoint_name, std::string token, bool* make_fileserv)
		: pipe(pipe), internet_connection(internet_connection), endpoint_name(endpoint_name),
		  token(token), make_fileserv(make_fileserv) {}
	SChannel(void)
		: pipe(NULL), internet_connection(false), make_fileserv(NULL) {}

	IPipe *pipe;
	bool internet_connection;
	std::string endpoint_name;
	std::string token;
	bool* make_fileserv;
};

struct SVolumesCache;

const unsigned int x_pingtimeout=180000;

class ClientConnector : public ICustomClient
{
public:
	ClientConnector(void);
	virtual void Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName);
	~ClientConnector(void);

	virtual bool Run(void);
	virtual void ReceivePackets(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	virtual bool wantReceive(void);

	virtual bool closeSocket( void );

	static int64 getLastTokenTime(const std::string & tok);

	void doQuitClient(void);
	bool isQuitting(void);
	void updatePCDone2(int nv);
	bool isHashdataOkay(void);
	void resetImageBackupStatus(void);

	void setIsInternetConnection(void);

	static bool isBackupRunning();

	static bool tochannelSendChanges(const char* changes, size_t changes_size);

	static bool tochannelLog(int64 log_id, const std::string& msg, int loglevel, const std::string& identity);

	static bool tochannelLog(int64 log_id, const std::wstring& msg, int loglevel, const std::string& identity);

	static void updateRestorePc(int64 status_id, int nv, const std::string& identity);

	static bool restoreDone(int64 log_id, int64 status_id, int64 restore_id, bool success, const std::string& identity);

	static IPipe* getFileServConnection(const std::string& server_token, unsigned int timeoutms);

private:
	bool checkPassword(const std::wstring &cmd, bool& change_pw);
	bool saveBackupDirs(str_map &args, bool server_default=false);
	void updateLastBackup(void);
	std::string replaceChars(std::string in);
	void updateSettings(const std::string &pData);
	void replaceSettings(const std::string &pData);
	void saveLogdata(const std::string &created, const std::string &pData);
	std::string getLogpoints(void);
	void getLogLevel(int logid, int loglevel, std::string &data);
	bool waitForThread(void);
	bool sendFullImage(void);
	bool sendIncrImage(void);
	bool sendMBR(std::wstring dl, std::wstring &errmsg);
    std::string receivePacket(const SChannel& channel);
	void downloadImage(str_map params);
	void removeChannelpipe(IPipe *cp);
	void waitForPings(IScopedLock *lock);
	bool writeUpdateFile(IFile *datafile, std::string outfn);
	std::string getSha512Hash(IFile *fn);
	bool checkHash(std::string shah);
	void tochannelSendStartbackup(RunningAction backup_type);
	void ImageErr(const std::string &msg);
	void update_silent(void);
	bool calculateFilehashesOnClient(void);
	void sendStatus();
    bool sendChannelPacket(const SChannel& channel, const std::string& msg);

	std::string getAccessTokensParams(const std::wstring& tokens, bool with_clientname);

	static bool sendMessageToChannel(const std::string& msg, int timeoutms, const std::string& identity);

	std::string getLastBackupTime();

	static std::string getCurrRunningJob();

	void CMD_ADD_IDENTITY(const std::string &identity, const std::string &cmd, bool ident_ok);
	void CMD_GET_CHALLENGE(const std::string &identity);
	void CMD_SIGNATURE(const std::string &identity, const std::string &cmd);
	void CMD_START_INCR_FILEBACKUP(const std::string &cmd);
	void CMD_START_FULL_FILEBACKUP(const std::string &cmd);
	void CMD_START_SHADOWCOPY(const std::string &cmd);
	void CMD_STOP_SHADOWCOPY(const std::string &cmd);
	void CMD_SET_INCRINTERVAL(const std::string &cmd);
	void CMD_GET_BACKUPDIRS(const std::string &cmd);
	void CMD_SAVE_BACKUPDIRS(const std::string &cmd, str_map &params);
	void CMD_GET_INCRINTERVAL(const std::string &cmd);
	void CMD_DID_BACKUP(const std::string &cmd);
	void CMD_STATUS(const std::string &cmd);
	void CMD_STATUS_DETAIL(const std::string &cmd);
	void CMD_UPDATE_SETTINGS(const std::string &cmd);
	void CMD_PING_RUNNING(const std::string &cmd);
	void CMD_PING_RUNNING2(const std::string &cmd);
	void CMD_CHANNEL(const std::string &cmd, IScopedLock *g_lock);
	void CMD_CHANNEL_PONG(const std::string &cmd);
	void CMD_CHANNEL_PING(const std::string &cmd);
	void CMD_TOCHANNEL_START_INCR_FILEBACKUP(const std::string &cmd);
	void CMD_TOCHANNEL_START_FULL_FILEBACKUP(const std::string &cmd);
	void CMD_TOCHANNEL_START_FULL_IMAGEBACKUP(const std::string &cmd);
	void CMD_TOCHANNEL_START_INCR_IMAGEBACKUP(const std::string &cmd);
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
	void CMD_RESTORE_DOWNLOAD_IMAGE(const std::string &cmd, str_map &params);
	void CMD_RESTORE_DOWNLOAD_FILES(const std::string &cmd, str_map &params);
	void CMD_RESTORE_DOWNLOADPROGRESS(const std::string &cmd);
	void CMD_RESTORE_LOGIN_FOR_DOWNLOAD(const std::string &cmd, str_map &params);
	void CMD_RESTORE_GET_SALT(const std::string &cmd, str_map &params);
	void CMD_VERSION_UPDATE(const std::string &cmd);
	void CMD_CLIENT_UPDATE(const std::string &cmd);
	void CMD_CAPA(const std::string &cmd);
	void CMD_NEW_SERVER(str_map &params);
	void CMD_ENABLE_END_TO_END_FILE_BACKUP_VERIFICATION(const std::string &cmd);
	void CMD_GET_VSSLOG(const std::string &cmd);
	void CMD_GET_ACCESS_PARAMS(str_map &params);
	void CMD_CONTINUOUS_WATCH_START();
	void CMD_SCRIPT_STDERR(const std::string& cmd);
	void CMD_FILE_RESTORE(const std::string& cmd);
	void CMD_RESTORE_OK(str_map &params);

	int getCapabilities();

	IPipe *pipe;
	IPipe *mempipe;
	bool mempipe_owner;
	THREAD_ID tid;
	ClientConnectorState state;
	int64 lasttime;
	int64 last_update_time;
	int file_version;
	CTCPStack tcpstack;
	volatile bool do_quit;
	bool is_channel;

	static RunningAction backup_running;
	static volatile bool backup_done;
	static IMutex *backup_mutex;
	static unsigned int incr_update_intervall;
	static int64 last_pingtime;
	static SChannel channel_pipe;
	static std::vector<SChannel> channel_pipes;
	static std::vector<IPipe*> channel_exit;
	static std::vector<IPipe*> channel_ping;
	static std::vector<int> channel_capa;
	int64 last_channel_ping;
	static int pcdone;
	static int64 eta_ms;
	static int pcdone2;
	static IMutex *progress_mutex;
	static volatile bool img_download_running;
	static db_results cached_status;
	static std::string backup_source_token;
	static std::map<std::string, int64> last_token_times;
	static int last_capa;
	static IMutex *ident_mutex;
	static std::vector<std::string> new_server_idents;
	static bool end_to_end_file_backup_verification_enabled;
	static std::map<std::string, std::string> challenges;
	static bool has_file_changes;
	static std::vector<std::pair<std::string, IPipe*> > fileserv_connections;
	static RestoreOkStatus restore_ok_status;
	static bool status_updated;

	IFile *hashdatafile;
	unsigned int hashdataleft;
	volatile bool hashdataok;
	bool silent_update;

	ImageInformation image_inf;

	std::string server_token;

	bool want_receive;

	std::vector<IPipe*> contractors;

	bool internet_conn;

	std::string endpoint_name;

	bool make_fileserv;

#ifdef _WIN32
	static SVolumesCache* volumes_cache;
#endif
};
