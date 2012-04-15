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

class ServerVHDWriter;
class IFile;
class IPipe;
class ServerPingThread;
class FileClient;

struct SBackup
{
	int incremental;
	std::wstring path;
	int incremental_ref;
};

class BackupServerGet : public IThread
{
public:
	BackupServerGet(IPipe *pPipe, sockaddr_in pAddr, const std::wstring &pName, bool internet_connection);
	~BackupServerGet(void);

	void operator()(void);

	bool sendClientMessage(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, bool logerr=true);
	std::string sendClientMessage(const std::string &msg, const std::wstring &errmsg, unsigned int timeout, bool logerr=true);
	void sendToPipe(const std::string &msg);
	int getPCDone(void);

	sockaddr_in getClientaddr(void);

	static void init_mutex(void);
	static void destroy_mutex(void);

	static bool isInBackupWindow(std::vector<STimeSpan> bw);
	static MailServer getMailServerSettings(void);

	static int getNumberOfRunningBackups(void);
	static int getNumberOfRunningFileBackups(void);

	IPipe *getClientCommandConnection(int timeoutms=10000);

private:
	void unloadSQL(void);
	void prepareSQL(void);
	int getClientID(void);
	void updateLastseen(void);
	bool isUpdateFull(void);
	bool isUpdateIncr(void);
	bool isUpdateFullImage(void);
	bool isUpdateIncrImage(void);
	bool isUpdateFullImage(const std::string &letter);
	bool isUpdateIncrImage(const std::string &letter);
	bool doFullBackup(bool with_hashes);
	int createBackupSQL(int incremental, int clientid, std::wstring path);
	void hashFile(std::wstring dstpath, std::wstring hashpath, IFile *fd, IFile *hashoutput, std::string old_file);
	void start_shadowcopy(const std::string &path);
	void stop_shadowcopy(const std::string &path);
	void notifyClientBackupSuccessfull(void);
	bool request_filelist_construct(bool full, bool with_token=true);
	bool load_file(const std::wstring &fn, const std::wstring &curr_path, FileClient &fc);
	bool load_file_patch(const std::wstring &fn, const std::wstring &curr_path, const std::wstring &last_backuppath, FileClientChunked &fc, FileClient &fc_normal);
	bool doIncrBackup(bool with_hashes, bool intra_file_diffs);
	SBackup getLastIncremental(void);
	bool hasChange(size_t line, const std::vector<size_t> &diffs);
	void updateLastBackup(void);
	void updateLastImageBackup(void);
	void sendClientBackupIncrIntervall(void);
	void sendSettings(void);
	bool getClientSettings(void);
	bool updateClientSetting(const std::wstring &key, const std::wstring &value);
	void setBackupComplete(void);
	void setBackupDone(void);
	void setBackupImageComplete(void);
	void sendClientLogdata(void);
	std::wstring getUserRights(int userid, std::string domain);
	void saveClientLogdata(int image, int incremental, bool r_success);
	void sendLogdataMail(bool r_success, int image, int incremental, int errors, int warnings, int infos, std::wstring &data);
	bool doImage(const std::string &pLetter, const std::wstring &pParentvhd, int incremental, int incremental_ref);
	std::string getMBR(const std::wstring &dl);
	unsigned int writeMBR(ServerVHDWriter *vhdfile, uint64 volsize);
	int createBackupImageSQL(int incremental, int incremental_ref, int clientid, std::wstring path, std::string letter);
	SBackup getLastIncrementalImage(const std::string &letter);
	void updateRunning(bool image);
	void checkClientVersion(void);
	bool sendFile(IPipe *cc, IFile *f, int timeout);
	bool isBackupsRunningOkay(void);
	void startBackupRunning(bool file);
	void stopBackupRunning(bool file);

	_u32 getClientFilesrvConnection(FileClient *fc, int timeoutms=10000);
	FileClientChunked getClientChunkedFilesrvConnection(int timeoutms=10000);

	void saveImageAssociation(int image_id, int assoc_id);
	
	std::wstring constructImagePath(const std::wstring &letter);
	bool constructBackupPath(bool with_hashes);
	void resetEntryState(void);
	bool getNextEntry(char ch, SFile &data);
	static std::string remLeadingZeros(std::string t);
	static std::string strftimeInt(std::string fs);
	bool updateCapabilities(void);


	_i64 getIncrementalSize(IFile *f, const std::vector<size_t> &diffs, bool all=false);

	void writeFileRepeat(IFile *f, const std::string &str);
	void writeFileRepeat(IFile *f, const char *buf, size_t bsize);

	int64 updateNextblock(int64 nextblock, int64 currblock, sha256_ctx *shactx, unsigned char *zeroblockdata, bool parent_fn, ServerVHDWriter *parentfile, IFile *hashfile, IFile *parenthashfile, unsigned int blocksize, int64 mbr_offset, int64 vhd_blocksize);

	std::wstring convertToOSPathFromFileClient(std::wstring path);
	IFile *getTemporaryFileRetry(void);
	void destroyTemporaryFile(IFile *tmp);

	IPipe *pipe;
	IDatabase *db;

	sockaddr_in clientaddr;
	IMutex *clientaddr_mutex;
	std::wstring clientname;
	
	std::wstring backuppath;
	std::wstring backuppath_hashes;
	std::wstring backuppath_single;

	int clientid;
	int backupid;

	ISettingsReader *settings;
	ISettingsReader *settings_client;
	ServerSettings *server_settings;

	IQuery *q_update_lastseen;
	IQuery *q_update_full;
	IQuery *q_update_incr;
	IQuery *q_create_backup;
	IQuery *q_get_last_incremental;
	IQuery *q_set_last_backup;
	IQuery *q_update_setting;
	IQuery *q_insert_setting;
	IQuery *q_set_complete;
	IQuery *q_update_image_full;
	IQuery *q_update_image_incr;
	IQuery *q_create_backup_image;
	IQuery *q_set_image_complete;
	IQuery *q_set_last_image_backup;
	IQuery *q_get_last_incremental_image;
	IQuery *q_set_image_size;
	IQuery *q_update_running_file;
	IQuery *q_update_running_image;
	IQuery *q_update_images_size;
	IQuery *q_set_done;
	IQuery *q_save_logdata;
	IQuery *q_get_unsent_logdata;
	IQuery *q_set_logdata_sent;
	IQuery *q_save_image_assoc;
	IQuery *q_get_users;
	IQuery *q_get_rights;
	IQuery *q_get_report_settings;
	IQuery *q_format_unixtime;


	int state;
	std::string t_name;

	int link_logcnt;

	IPipe *hashpipe;
	IPipe *hashpipe_prepare;
	IPipe *exitpipe;
	IPipe *exitpipe_prepare;

	ServerPingThread *pingthread;
	THREADPOOL_TICKET pingthread_ticket;

	SStatus status;
	bool has_error;

	bool r_incremental;
	
	bool can_backup_images;

	bool do_full_backup_now;
	bool do_incr_backup_now;
	bool do_update_settings;
	bool do_full_image_now;
	bool do_incr_image_now;

	static int running_backups;
	static int running_file_backups;
	static IMutex *running_backup_mutex;

	int filesrv_protocol_version;
	int file_protocol_version;

	bool internet_connection;

	CTCPStack tcpstack;
};
