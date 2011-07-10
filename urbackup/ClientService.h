#include "../Interface/Service.h"
#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "fileclient/tcpstack.h"

class ClientService : public IService
{
public:
	virtual ICustomClient* createClient();
	virtual void destroyClient( ICustomClient * pClient);
};

class ClientConnector : public ICustomClient, public IThread
{
public:
	virtual void Init(THREAD_ID pTID, IPipe *pPipe);
	~ClientConnector(void);

	virtual bool Run(void);
	virtual void ReceivePackets(void);

	static void init_mutex(void);

	virtual bool wantReceive(void);

	void operator()(void);

private:
	bool checkPassword(const std::wstring &cmd);
	void getBackupDirs(int version=0);
	bool saveBackupDirs(str_map &args, bool server_default=false);
	void updateLastBackup(void);
	void getBackupStatus(void);
	std::string replaceChars(std::string in);
	void updateSettings(const std::string &pData);
	void replaceSettings(const std::string &pData);
	void saveLogdata(const std::string &created, const std::string &pData);
	std::string getLogpoints(void);
	void getLogLevel(int logid, int loglevel, std::string &data);
	bool sendFullImage(void);
	bool sendIncrImage(void);
	bool waitForThread(void);
	void sendFullImageThread(void);
	void sendIncrImageThread(void);
	void ImageErr(const std::string &msg);
	void ImageErrRunning(const std::string &msg);
	void removeShadowCopyThread(int save_id);
	bool sendMBR(const std::wstring &dl);
	std::string receivePacket(IPipe *p);
	void downloadImage(str_map params);
	void removeChannelpipe(IPipe *cp);
	void waitForPings(IScopedLock *lock);
	bool writeUpdateFile(IFile *datafile, std::string outfn);
	std::string getSha512Hash(IFile *fn);
	bool checkHash(std::string shah);

	IPipe *pipe;
	THREAD_ID tid;
	int state;
	IPipe *mempipe;
	unsigned int lasttime;
	CTCPStack tcpstack;
	volatile bool do_quit;
	bool is_channel;

	static int backup_running;
	static volatile bool backup_done;
	static IMutex *backup_mutex;
	static unsigned int incr_update_intervall;
	static unsigned int last_pingtime;
	static IPipe *channel_pipe;
	static std::vector<IPipe*> channel_pipes;
	static std::vector<IPipe*> channel_exit;
	static std::vector<IPipe*> channel_ping;
	static unsigned int last_channel_ping;
	static int pcdone;
	static int pcdone2;
	static IMutex *progress_mutex;
	static volatile bool img_download_running;
	static db_results cached_status;
	static std::string backup_source_token;

	std::string image_letter;

	int thread_action;
	THREADPOOL_TICKET thread_ticket;
	std::string thread_ret;
	std::string shadowdrive;
	uint64 startpos;
	int shadow_id;
	IFile *hashdatafile;
	unsigned int hashdataleft;
	volatile bool hashdataok;

	std::string server_token;

	bool want_receive;

	std::vector<IPipe*> contractors;
};