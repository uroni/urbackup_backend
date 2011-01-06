#include "../Interface/Pipe.h"
#include "../Interface/Query.h"
#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "watchdir/DirectoryChanges.h"
#include "Database.h"
#include "ChangeJournalWatcher.h"
#include <list>

struct SLastEntries
{
	std::wstring dir;
	unsigned int time;
};

class DirectoryWatcherThread : public IThread, public IChangeJournalListener
{
public:
	DirectoryWatcherThread(const std::vector<std::wstring> &watchdirs);

	void operator()(void);

	static IPipe *getPipe(void);

	void stop(void);

	void On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName);
    void On_FileRemoved(const std::wstring & strFileName);
    void On_FileAdded(const std::wstring & strFileName);
    void On_FileModified(const std::wstring & strFileName);
	void On_ResetAll(const std::wstring &vol);

	void OnDirMod(const std::wstring &dir);

private:
	static IPipe *pipe;
	IDatabase *db;

	bool do_stop;
	
	IQuery* q_add_dir;

	std::list<SLastEntries> lastentries;
	std::vector<std::wstring> watching;
};

class ChangeListener : public CDirectoryChangeHandler, public IChangeJournalListener
{
public:
	void On_FileNameChanged(const std::wstring & strOldFileName, const std::wstring & strNewFileName);
    void On_FileRemoved(const std::wstring & strFileName);
    void On_FileAdded(const std::wstring & strFileName);
    void On_FileModified(const std::wstring & strFileName);
	void On_ResetAll(const std::wstring &vol);
};