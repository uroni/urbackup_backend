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
	unsigned int time;
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

	bool is_stopped(void);

private:
	static IPipe *pipe;
	IDatabase *db;

	volatile bool do_stop;
	
	IQuery* q_get_dir;
	IQuery* q_add_dir;
	IQuery* q_add_del_dir;
	IQuery* q_add_file;

	std::list<SLastEntries> lastentries;
	std::vector<std::wstring> watching;

	static IMutex *update_mutex;
	static ICondition *update_cond;
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