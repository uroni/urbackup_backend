#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Pipe.h"
#include "../Interface/Database.h"
#include "../Interface/ThreadPool.h"
#include "../urbackupcommon/os_functions.h"
#include "clientdao.h"
#include <map>
#include "tokens.h"

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

#ifndef _WIN32
#define VSS_ID GUID
#endif

#include "../fileservplugin/IFileServFactory.h"

#include <vector>
#include <string>
#include <fstream>
#include <sstream>

const int c_group_default = 0;
const int c_group_continuous = 1;
const int c_group_max = 99;
const int c_group_size = 100;

const unsigned int flag_end_to_end_verification = 2;
const unsigned int flag_with_scripts = 4;
const unsigned int flag_calc_checksums = 8;
const unsigned int flag_with_orig_path = 16;
const unsigned int flag_with_sequence = 32;
const unsigned int flag_with_proper_symlinks = 64;

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
	SCRef(void): backupcom(NULL), ok(false), dontincrement(false) {}

	IVssBackupComponents *backupcom;
#endif
	VSS_ID ssetid;
	std::string volpath;
	int64 starttime;
	std::string target;
	int save_id;
	bool ok;
	bool dontincrement;
	std::vector<std::string> starttokens;
	std::string clientsubname;
};

struct SCDirs
{
	SCDirs(void): ref(NULL) {}
	std::string dir;
	std::string target;
	std::string orig_target;
	bool running;
	SCRef *ref;
	int64 starttime;
	bool fileserv;
};

struct SHashedFile
{
	SHashedFile(const std::string& path,
				_i64 filesize,
				_i64 modifytime,
				const std::string& hash)
				: path(path),
				  filesize(filesize), modifytime(modifytime),
				  hash(hash)
	{
	}

	std::string path;
	_i64 filesize;
	_i64 modifytime;
	std::string hash;
};

struct SShadowCopyContext
{
#ifdef _WIN32
	SShadowCopyContext()
		:backupcom(NULL)
	{

	}

	IVssBackupComponents *backupcom;
#endif
};

struct SVssLogItem
{
	std::string msg;
	int loglevel;
	int64 times;
};

struct SBackupScript
{
	std::string scriptname;
	std::string outputname;
	int64 size;

	bool operator<(const SBackupScript &other) const
	{
		return outputname < other.outputname;
	}
};

class ClientDAO;

class IndexThread : public IThread
{
public:
	static const char IndexThreadAction_StartFullFileBackup;
	static const char IndexThreadAction_StartIncrFileBackup;
	static const char IndexThreadAction_CreateShadowcopy;
	static const char IndexThreadAction_ReleaseShadowcopy;
	static const char IndexThreadAction_GetLog;
	static const char IndexThreadAction_PingShadowCopy;
	static const char IndexThreadAction_AddWatchdir;
	static const char IndexThreadAction_RemoveWatchdir;

	IndexThread(void);
	~IndexThread();

	void operator()(void);

	static IMutex* getFilelistMutex(void);
	static IPipe * getMsgPipe(void);
	static IFileServ *getFileSrv(void);

	static void stopIndex(void);

	static void shareDir(const std::string& token, std::string name, const std::string &path);
	static void removeDir(const std::string& token, std::string name);
	static std::string getShareDir(const std::string &name);
	static void share_dirs();
	static void unshare_dirs();
	
	static void execute_postbackup_hook(void);

	static void doStop(void);
	
	static bool backgroundBackupsEnabled(const std::string& clientsubname);

	static std::vector<std::string> parseExcludePatterns(const std::string& val);
	static std::vector<std::string> parseIncludePatterns(const std::string& val, std::vector<int>& include_depth,
		std::vector<std::string>& include_prefix);

	static bool isExcluded(const std::vector<std::string>& exlude_dirs, const std::string &path);
	static bool isIncluded(const std::vector<std::string>& include_dirs, const std::vector<int>& include_depth,
		const std::vector<std::string>& include_prefix, const std::string &path, bool *adding_worthless);

	static std::string mapScriptOutputName(const std::string& fn);

	static std::string getSHA256(const std::string& fn);

	class IHashFunc
	{
	public:
		virtual void hash(const char* buf, _u32 bsize) = 0;
		virtual void sparse_hash(const char* buf, _u32 bsize) = 0;
	};

private:

	bool readBackupDirs(void);
	bool readBackupScripts();

    bool getAbsSymlinkTarget(const std::string& symlink, const std::string& orig_path, std::string& target);
	void addSymlinkBackupDir(const std::string& target);
	bool backupNameInUse(const std::string& name);
	void removeUnconfirmedSymlinkDirs(size_t off);

	std::vector<SFileAndHash> convertToFileAndHash(const std::string& orig_dir, const std::vector<SFile> files, const std::string& fn_filter);

	bool initialCheck(std::string orig_dir, std::string dir, std::string named_path, std::fstream &outfile, bool first, int flags, bool use_db, bool symlinked);

	void indexDirs(void);

	void updateDirs(void);

	static std::string sanitizePattern(const std::string &p);
	void readPatterns();	

	std::vector<SFileAndHash> getFilesProxy(const std::string &orig_path, std::string path, const std::string& named_path, bool use_db, const std::string& fn_filter);

	bool start_shadowcopy(SCDirs *dir, bool *onlyref=NULL, bool allow_restart=false, std::vector<SCRef*> no_restart_refs=std::vector<SCRef*>(), bool for_imagebackup=false, bool *stale_shadowcopy=NULL);

	bool find_existing_shadowcopy(SCDirs *dir, bool *onlyref, bool allow_restart, const std::string& wpath, const std::vector<SCRef*>& no_restart_refs, bool for_imagebackup, bool *stale_shadowcopy,
		bool consider_only_own_tokens);
	bool release_shadowcopy(SCDirs *dir, bool for_imagebackup=false, int save_id=-1, SCDirs *dontdel=NULL);
	bool cleanup_saved_shadowcopies(bool start=false);
	std::string lookup_shadowcopy(int sid);
#ifdef _WIN32
	bool start_shadowcopy_win( SCDirs * dir, std::string &wpath, bool for_imagebackup, bool * &onlyref );
	bool wait_for(IVssAsync *vsasync);
	std::string GetErrorHResErrStr(HRESULT res);
	bool check_writer_status(IVssBackupComponents *backupcom, std::string& errmsg, int loglevel, bool* retryable_error);
	bool checkErrorAndLog(BSTR pbstrWriter, VSS_WRITER_STATE pState, HRESULT pHrResultFailure, std::string& errmsg, int loglevel, bool* retryable_error);
#else
	bool start_shadowcopy_lin( SCDirs * dir, std::string &wpath, bool for_imagebackup, bool * &onlyref );
#endif

	bool deleteShadowcopy(SCDirs *dir);
	bool deleteSavedShadowCopy(SShadowCopy& scs, SShadowCopyContext& context);
	void clearContext( SShadowCopyContext& context);

	
	void VSSLog(const std::string& msg, int loglevel);
	void VSSLogLines(const std::string& msg, int loglevel);

	SCDirs* getSCDir(const std::string& path, const std::string& clientsubname);

	int execute_hook(std::string script_name, bool incr, std::string server_token, int index_group);
	int execute_prebackup_hook(bool incr, std::string server_token, int index_group);
	int execute_postindex_hook(bool incr, std::string server_token, int index_group);
	std::string execute_script(const std::string& cmd);

	void start_filesrv(void);

	bool skipFile(const std::string& filepath, const std::string& namedpath);

	bool addMissingHashes(std::vector<SFileAndHash>* dbfiles, std::vector<SFileAndHash>* fsfiles, const std::string &orig_path, const std::string& filepath, const std::string& namedpath);

	void modifyFilesInt(std::string path, int tgroup, const std::vector<SFileAndHash> &data);
	size_t calcBufferSize( std::string &path, const std::vector<SFileAndHash> &data );

	void commitModifyFilesBuffer();

	void addFilesInt(std::string path, int tgroup, const std::vector<SFileAndHash> &data);
	void commitAddFilesBuffer();
	std::string getShaBinary(const std::string& fn);

	std::string removeDirectorySeparatorAtEnd(const std::string& path);

	bool getShaBinary(const std::string& fn, IHashFunc& hf);

	std::string addDirectorySeparatorAtEnd(const std::string& path);

	void resetFileEntries(void);

	static void addFileExceptions(std::vector<std::string>& exclude_dirs);

	static void addHardExcludes(std::vector<std::string>& exclude_dirs);

	void handleHardLinks(const std::string& bpath, const std::string& vsspath);

	std::string escapeListName(const std::string& listname);

	std::string escapeDirParam(const std::string& dir);

	void writeTokens();

	void setFlags(unsigned int flags);

	void writeDir(std::fstream& out, const std::string& name, bool with_change, int64 change_identicator, const std::string& extra=std::string());
	bool addBackupScripts(std::fstream& outfile);

	void monitor_disk_failures();

	int get_db_tgroup();

	std::string starttoken;

	std::vector<SBackupDir> backup_dirs;

	std::vector<std::string> changed_dirs;
	std::vector<std::string> open_files;

	static IMutex *filelist_mutex;
	static IMutex *filesrv_mutex;

	static IPipe* msgpipe;

	IPipe *contractor;

	ClientDAO *cd;

	IDatabase *db;

	static IFileServ *filesrv;

	DirectoryWatcherThread *dwt;
	THREADPOOL_TICKET dwt_ticket;

	std::map<std::pair<std::string, std::string>, std::map<std::string, SCDirs*> > scdirs;
	std::vector<SCRef*> sc_refs;

	int index_c_db;
	int index_c_fs;
	int index_c_db_update;

	static volatile bool stop_index;

	std::vector<std::string> exlude_dirs;
	std::vector<std::string> include_dirs;
	std::vector<int> include_depth;
	std::vector<std::string> include_prefix;

	int64 last_transaction_start;

	static std::map<std::string, std::string> filesrv_share_dirs;

	struct SBufferItem
	{
		SBufferItem(std::string path, int tgroup, std::vector<SFileAndHash> files)
			: path(path), tgroup(tgroup), files(files)
		{}

		std::string path;
		int tgroup;
		std::vector<SFileAndHash> files;
	};

	std::vector< SBufferItem > modify_file_buffer;
	size_t modify_file_buffer_size;
	std::vector< SBufferItem > add_file_buffer;
	size_t add_file_buffer_size;

	int64 last_file_buffer_commit_time;

	int index_group;
	int index_flags;
	std::string index_clientsubname;
	bool index_server_default;

	SCDirs* index_scd;

	bool end_to_end_file_backup_verification;
	bool calculate_filehashes_on_client;
	bool with_scripts;
	bool with_orig_path;
	bool with_sequence;
	bool with_proper_symlinks;

	int64 last_tmp_update_time;

	std::string index_root_path;
	bool index_error;

	std::vector<SVssLogItem> vsslog;

	std::vector<SBackupScript> scripts;

	_i64 last_filebackup_filetime;

	tokens::TokenCache token_cache;
	int sha_version;
};

std::string add_trailing_slash(const std::string &strDirName);
