#include "../Interface/Pipe.h"
#include "../Interface/Query.h"
#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#ifdef _WIN32
#include "watchdir/DirectoryChanges.h"
#endif
#include "database.h"
#include "ChangeJournalWatcher.h"
#include <list>

struct SLastEntries
{
	std::wstring dir;
	std::wstring fn;
	int64 time;
};

class DirectoryWatcherThread : public IThread, public IChangeJournalListener
{
public:
	DirectoryWatcherThread(const std::vector<std::wstring> &watchdirs);

	static void init_mutex(void);

	void operator()(void);

	static IPipe *getPipe(void);

	void stop(void);

	void On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName);
    void On_FileRemoved(const std::wstring & strFileName);
    void On_FileAdded(const std::wstring & strFileName);
    void On_FileModified(const std::wstring & strFileName, bool save_fn);
	void On_ResetAll(const std::wstring &vol);
	void On_DirRemoved(const std::wstring & strDirName);

	void OnDirMod(const std::wstring &dir, const std::wstring &fn);
	void OnDirRm(const std::wstring &dir);

	static void update(void);
	static void update_and_wait(void);

	static void freeze(void);
	static void unfreeze(void);

	bool is_stopped(void);

	static void update_last_backup_time(void);
	static void commit_last_backup_time(void);

	static _i64 get_current_filetime();

private:

	std::wstring addSlashIfMissing(const std::wstring &strDirName);

	static IPipe *pipe;
	IDatabase *db;

	volatile bool do_stop;
	
	IQuery* q_get_dir;
	IQuery* q_add_dir;
	IQuery* q_add_dir_with_id;
	IQuery* q_add_del_dir;
	IQuery* q_add_file;
	IQuery* q_get_dir_backup;
	IQuery* q_update_last_backup_time;

	std::list<SLastEntries> lastentries;
	std::vector<std::wstring> watching;

	static IMutex *update_mutex;
	static ICondition *update_cond;

	int64 last_backup_filetime;
};

class ChangeListener : public CDirectoryChangeHandler, public IChangeJournalListener
{
public:
	void On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName);
    void On_FileRemoved(const std::wstring & strFileName);
    void On_FileAdded(const std::wstring & strFileName);
    void On_FileModified(const std::wstring & strFileName, bool save_fn);
	void On_ResetAll(const std::wstring &vol);
	void On_DirRemoved(const std::wstring & strDirName);
};