#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Pipe.h"
#include "../Interface/Database.h"
#include "../Interface/ThreadPool.h"
#include "os_functions.h"
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

class ClientDAO;

class IndexThread : public IThread
{
public:
	IndexThread(void);

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

private:

	void readBackupDirs(void);
	void initialCheck(const std::wstring &orig_dir, const std::wstring &dir, std::fstream &outfile, bool first=false);

	void indexDirs(void);

	void updateDirs(void);

	void readExcludePattern(void);
	bool isExcluded(const std::wstring &path);

	std::vector<SFile> getFilesProxy(const std::wstring &orig_path, const std::wstring &path, bool use_db=true);

	bool start_shadowcopy(SCDirs *dir, bool *onlyref=NULL, bool restart_own=false, std::vector<SCRef*> no_restart_refs=std::vector<SCRef*>(), bool for_imagebackup=false );
	bool release_shadowcopy(SCDirs *dir, bool for_imagebackup=false, int save_id=-1, SCDirs *dontdel=NULL);
	bool cleanup_saved_shadowcopies(bool start=false);
	std::string lookup_shadowcopy(int sid);
#ifdef _WIN32
	bool wait_for(IVssAsync *vsasync);
	std::string GetErrorHResErrStr(HRESULT res);
#endif

	SCDirs* getSCDir(const std::wstring path);

	void execute_prebackup_hook(void);
	void execute_postindex_hook(void);

	void start_filesrv(void);

	std::string starttoken;

	std::vector<SBackupDir> backup_dirs;

	std::vector<std::wstring> changed_dirs;

	static IMutex *filelist_mutex;
	static IMutex *filesrv_mutex;

	static IPipe* msgpipe;

	IPipe *contractor;

	ClientDAO *cd;

	IDatabase *db;

	static IFileServ *filesrv;

	DirectoryWatcherThread *dwt;
	THREADPOOL_TICKET dwt_ticket;

	std::map<std::wstring, SCDirs*> scdirs;
	std::vector<SCRef*> sc_refs;

	int index_c_db;
	int index_c_fs;
	int index_c_db_update;

	static volatile bool stop_index;

	std::vector<std::wstring> exlude_dirs;

	unsigned int last_transaction_start;

	static std::map<std::wstring, std::wstring> filesrv_share_dirs;
};
