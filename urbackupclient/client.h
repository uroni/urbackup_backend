#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Pipe.h"
#include "../Interface/Database.h"
#include "../Interface/ThreadPool.h"
#include "../urbackupcommon/os_functions.h"
#include "clientdao.h"
#include <map>

#ifdef _WIN32
#ifndef VSS_XP
#ifndef VSS_S03
#include <Vss.h>
#include <VsWriter.h>
#include <VsBackup.h>
#else
#include <win2003/vss.h>
#include <win2003/vswriter.h>
#include <win2003/vsbackup.h>
#endif //VSS_S03
#else
#include <winxp/vss.h>
#include <winxp/vswriter.h>
#include <winxp/vsbackup.h>
#endif //VSS_XP
#endif

#include "../fileservplugin/IFileServFactory.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>

class DirectoryWatcherThread;

class IdleCheckerThread : public IThread
{
public:
	void operator()(void);

	static bool getIdle(void);
	static bool getPause(void);
	static void setPause(bool b);

private:
	volatile static bool idle;
	volatile static bool pause;
};

struct SCRef
{
#ifdef _WIN32
	SCRef(void): rcount(0), backupcom(NULL), ok(false), dontincrement(false) {}

	IVssBackupComponents *backupcom;
	VSS_ID ssetid;
#endif
	int rcount;
	std::wstring volpath;
	unsigned int starttime;
	std::wstring target;
	int save_id;
	bool ok;
	bool dontincrement;
	std::vector<std::string> starttokens;
};

struct SCDirs
{
	SCDirs(void): ref(NULL) {}
	std::wstring dir;
	std::wstring target;
	std::wstring orig_target;
	bool running;
	SCRef *ref;
	unsigned int starttime;
	bool fileserv;
};

struct SHashedFile
{
	SHashedFile(const std::wstring& path,
				_i64 filesize,
				_i64 modifytime,
				const std::string& hash)
				: path(path),
				  filesize(filesize), modifytime(modifytime),
				  hash(hash)
	{
	}

	std::wstring path;
	_i64 filesize;
	_i64 modifytime;
	std::string hash;
};

class ClientDAO;

class IndexThread : public IThread
{
public:
	static const char IndexThreadAction_GetLog;

	IndexThread(void);
	~IndexThread();

	void operator()(void);

	static IMutex* getFilelistMutex(void);
	static IPipe * getMsgPipe(void);
	static IFileServ *getFileSrv(void);

	static void stopIndex(void);

	static void shareDir(const std::wstring &name, const std::wstring &path);
	static void removeDir(const std::wstring &name);
	static std::wstring getShareDir(const std::wstring &name);
	static void share_dirs(const std::string &token);
	static void unshare_dirs(const std::string &token);
	
	static void execute_postbackup_hook(void);

	static void doStop(void);

private:

	void readBackupDirs(void);
	bool initialCheck(const std::wstring &orig_dir, const std::wstring &dir, const std::wstring &named_path, std::fstream &outfile, bool first=false);

	void indexDirs(void);

	void updateDirs(void);

	std::wstring sanitizePattern(const std::wstring &p);
	void readPatterns(bool &pattern_changed, bool update_saved_patterns);
	bool isExcluded(const std::wstring &path);
	bool isIncluded(const std::wstring &path, bool *adding_worthless);

	std::vector<SFileAndHash> getFilesProxy(const std::wstring &orig_path, std::wstring path, const std::wstring& named_path, bool use_db=true);

	bool start_shadowcopy(SCDirs *dir, bool *onlyref=NULL, bool allow_restart=false, std::vector<SCRef*> no_restart_refs=std::vector<SCRef*>(), bool for_imagebackup=false, bool *stale_shadowcopy=NULL);
	bool release_shadowcopy(SCDirs *dir, bool for_imagebackup=false, int save_id=-1, SCDirs *dontdel=NULL);
	bool cleanup_saved_shadowcopies(bool start=false);
	std::string lookup_shadowcopy(int sid);
#ifdef _WIN32
	bool wait_for(IVssAsync *vsasync);
	std::string GetErrorHResErrStr(HRESULT res);
	bool check_writer_status(IVssBackupComponents *backupcom, std::wstring& errmsg, int loglevel, bool* retryable_error);
	bool checkErrorAndLog(BSTR pbstrWriter, VSS_WRITER_STATE pState, HRESULT pHrResultFailure, std::wstring& errmsg, int loglevel, bool* retryable_error);
#endif
	
	void VSSLog(const std::string& msg, int loglevel);
	void VSSLog(const std::wstring& msg, int loglevel);

	SCDirs* getSCDir(const std::wstring path);

	void execute_prebackup_hook(void);
	void execute_postindex_hook(void);

	void start_filesrv(void);

	bool skipFile(const std::wstring& filepath, const std::wstring& namedpath);

	bool addMissingHashes(std::vector<SFileAndHash>* dbfiles, std::vector<SFileAndHash>* fsfiles, const std::wstring &orig_path, const std::wstring& filepath, const std::wstring& namedpath);

	void modifyFilesInt(std::wstring path, const std::vector<SFileAndHash> &data);
	void commitModifyFilesBuffer(void);

	std::wstring removeDirectorySeparatorAtEnd(const std::wstring& path);

	std::string getSHA256(const std::wstring& fn);
	std::string getSHA512Binary(const std::wstring& fn);

	void resetFileEntries(void);

	void addFileExceptions(void);

	void handleHardLinks(const std::wstring& bpath, const std::wstring& vsspath);

	std::string escapeListName(const std::string& listname);

	std::string starttoken;

	std::vector<SBackupDir> backup_dirs;

	std::vector<SMDir> changed_dirs;

	static IMutex *filelist_mutex;
	static IMutex *filesrv_mutex;

	static IPipe* msgpipe;

	IPipe *contractor;

	ClientDAO *cd;

	IDatabase *db;

	static IFileServ *filesrv;

	DirectoryWatcherThread *dwt;
	THREADPOOL_TICKET dwt_ticket;

	std::map<std::string, std::map<std::wstring, SCDirs*> > scdirs;
	std::vector<SCRef*> sc_refs;

	int index_c_db;
	int index_c_fs;
	int index_c_db_update;

	static volatile bool stop_index;

	std::vector<std::wstring> exlude_dirs;
	std::vector<std::wstring> include_dirs;
	std::vector<int> include_depth;
	std::vector<std::wstring> include_prefix;

	unsigned int last_transaction_start;

	static std::map<std::wstring, std::wstring> filesrv_share_dirs;

	std::vector< std::pair<std::wstring, std::vector<SFileAndHash> > > modify_file_buffer;
	std::vector< SHashedFile > modify_hash_buffer;
	size_t modify_file_buffer_size;
	size_t modify_hash_buffer_size;

	int end_to_end_file_backup_verification_enabled;
	int calculate_filehashes_on_client;

	unsigned int last_tmp_update_time;

	std::wstring index_root_path;
	bool index_error;

	std::vector<std::pair<std::string, int> > vsslog;
};
