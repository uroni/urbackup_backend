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
	CCSTATE_WAIT_FOR_CONTRACTORS=8
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
	RUNNING_INCR_IMAGE=4
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
};

struct SChannel
{
	SChannel(IPipe *pipe, bool internet_connection)
		: pipe(pipe), internet_connection(internet_connection) {}
	SChannel(void)
		: pipe(NULL), internet_connection(false) {}

	IPipe *pipe;
	bool internet_connection;
};

const unsigned int x_pingtimeout=180000;

class ClientConnector : public ICustomClient
{
public:
	ClientConnector(void);
	virtual void Init(THREAD_ID pTID, IPipe *pPipe);
	~ClientConnector(void);

	virtual bool Run(void);
	virtual void ReceivePackets(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	virtual bool wantReceive(void);


	static unsigned int getLastTokenTime(const std::string & tok);

	void doQuitClient(void);
	bool isQuitting(void);
	void updatePCDone2(int nv);
	bool isHashdataOkay(void);
	void resetImageBackupStatus(void);

	void setIsInternetConnection(void);

private:
	bool checkPassword(const std::wstring &cmd);
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
	std::string receivePacket(IPipe *p);
	void downloadImage(str_map params);
	void removeChannelpipe(IPipe *cp);
	void waitForPings(IScopedLock *lock);
	bool writeUpdateFile(IFile *datafile, std::string outfn);
	std::string getSha512Hash(IFile *fn);
	bool checkHash(std::string shah);
	void tochannelSendStartbackup(RunningAction backup_type);
	void ImageErr(const std::string &msg);
	void update_silent(void);

	void CMD_ADD_IDENTITY(const std::string &identity, const std::string &cmd, bool ident_ok);
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
	void CMD_UPDATE_SETTINGS(const std::string &cmd);
	void CMD_PING_RUNNING(const std::string &cmd);
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
	void CMD_RESTORE_DOWNLOAD_IMAGE(const std::string &cmd, str_map &params);
	void CMD_RESTORE_DOWNLOADPROGRESS(const std::string &cmd);
	void CMD_VERSION_UPDATE(const std::string &cmd);
	void CMD_CLIENT_UPDATE(const std::string &cmd);
	void CMD_CAPA(const std::string &cmd);
	void CMD_NEW_SERVER(str_map &params);
	void CMD_ENABLE_END_TO_END_FILE_BACKUP_VERIFICATION(const std::string &cmd);
	void CMD_GET_VSSLOG(const std::string &cmd);

	IPipe *pipe;
	IPipe *mempipe;
	THREAD_ID tid;
	ClientConnectorState state;
	unsigned int lasttime;
	unsigned int last_update_time;
	int file_version;
	CTCPStack tcpstack;
	volatile bool do_quit;
	bool is_channel;

	static RunningAction backup_running;
	static volatile bool backup_done;
	static IMutex *backup_mutex;
	static unsigned int incr_update_intervall;
	static unsigned int last_pingtime;
	static SChannel channel_pipe;
	static std::vector<SChannel> channel_pipes;
	static std::vector<IPipe*> channel_exit;
	static std::vector<IPipe*> channel_ping;
	static std::vector<int> channel_capa;
	unsigned int last_channel_ping;
	static int pcdone;
	static int pcdone2;
	static IMutex *progress_mutex;
	static volatile bool img_download_running;
	static db_results cached_status;
	static std::string backup_source_token;
	static std::map<std::string, unsigned int> last_token_times;
	static int last_capa;
	static IMutex *ident_mutex;
	static std::vector<std::string> new_server_idents;
	static bool end_to_end_file_backup_verification_enabled;

	IFile *hashdatafile;
	unsigned int hashdataleft;
	volatile bool hashdataok;
	bool silent_update;

	ImageInformation image_inf;

	std::string server_token;

	bool want_receive;

	std::vector<IPipe*> contractors;

	bool internet_conn;

	
};
