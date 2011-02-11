#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/Mutex.h"
#include "../Interface/ThreadPool.h"
#include "fileclient/FileClient.h"
#include "os_functions.h"
#include "server_hash.h"
#include "server_prepare_hash.h"
#include "server_status.h"
#include "sha2/sha2.h"
#include "server_settings.h"

class ServerVHDWriter;
class IFile;
class IPipe;
class ServerPingThread;

enum D_TYPE
{
	DT_DELETED, DT_ADDED, DT_CHANGED
};

struct SDiff
{
	std::string entry;
	int line;
	D_TYPE type;

	bool operator<(const SDiff &other) const
	{
		return line < other.line;
	}
};

struct SBackup
{
	int incremental;
	std::wstring path;
	int incremental_ref;
};

class BackupServerGet : public IThread
{
public:
	BackupServerGet(IPipe *pPipe, sockaddr_in pAddr, const std::string &pName);
	~BackupServerGet(void);

	void operator()(void);

	bool sendClientMessage(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, bool logerr=true);
	std::string sendClientMessage(const std::string &msg, const std::wstring &errmsg, unsigned int timeout, bool logerr=true);
	void sendToPipe(const std::string &msg);
	int getPCDone(void);

private:
	void unloadSQL(void);
	void prepareSQL(void);
	int getClientID(void);
	void updateLastseen(void);
	bool isUpdateFull(void);
	bool isUpdateIncr(void);
	bool isUpdateFullImage(void);
	bool isUpdateIncrImage(void);
	bool doFullBackup(void);
	int createBackupSQL(int incremental, int clientid, std::wstring path);
	void hashFile(std::wstring dstpath, IFile *fd);
	void start_shadowcopy(const std::string &path);
	void stop_shadowcopy(const std::string &path);
	void notifyClientBackupSuccessfull(void);
	std::vector<SDiff> diffFiles(std::string pInput, std::string pOutput);
	bool request_filelist_construct(bool full);
	bool load_file(const std::wstring &fn, const std::wstring &curr_path, FileClient &fc);
	bool doIncrBackup(void);
	SBackup getLastIncremental(void);
	bool hasChange(int line, const std::vector<SDiff> &diffs);
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
	void saveClientLogdata(int image, int incremental);
	bool doImage(const std::wstring &pParentvhd, int incremental, int incremental_ref);
	std::string getMBR(const std::wstring &dl);
	unsigned int writeMBR(ServerVHDWriter *vhdfile, uint64 volsize);
	int createBackupImageSQL(int incremental, int incremental_ref, int clientid, std::wstring path);
	SBackup getLastIncrementalImage(void);
	void updateRunning(bool image);
	void checkClientVersion(void);
	bool sendFile(IPipe *cc, IFile *f, int timeout);
	
	std::wstring constructImagePath(const std::wstring &letter);
	bool constructBackupPath(void);
	void resetEntryState(void);
	bool getNextEntry(char ch, SFile &data);

	sockaddr_in getClientaddr(void);

	int64 updateNextblock(int64 nextblock, int64 currblock, sha256_ctx *shactx, unsigned char *zeroblockdata, bool parent_fn, ServerVHDWriter *parentfile, IFile *hashfile, IFile *parenthashfile, unsigned int blocksize, int64 mbr_offset, int64 vhd_blocksize);

	IPipe *pipe;
	IDatabase *db;

	sockaddr_in clientaddr;
	IMutex *clientaddr_mutex;
	std::string clientname;
	
	std::wstring backuppath;
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

	bool do_full_backup_now;
	bool do_incr_backup_now;
	bool do_update_settings;
	bool do_full_image_now;
	bool do_incr_image_now;
};
