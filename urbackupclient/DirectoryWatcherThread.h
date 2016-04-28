#include "../Interface/Pipe.h"
#include "../Interface/Query.h"
#include "../Interface/Thread.h"
#include "../Interface/Database.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "database.h"
#include "ChangeJournalWatcher.h"
#include "watchdir/JournalDAO.h"
#include <list>
#include "watchdir/ContinuousWatchEnqueue.h"

struct SLastEntries
{
	std::string dir;
	int64 time;
};

class DirectoryWatcherThread : public IThread, public IChangeJournalListener
{
public:
	DirectoryWatcherThread(const std::vector<std::string> &watchdirs,
		const std::vector<ContinuousWatchEnqueue::SWatchItem> &watchdirs_continuous);

	static void init_mutex(void);

	void operator()(void);

	static IPipe *getPipe(void);

	void stop(void);

	virtual int64 getStartUsn(int64 sequence_id);
	void On_FileNameChanged(const std::string & strOldFileName, const std::string & strNewFileName, bool closed);
	void On_DirNameChanged( const std::string & strOldFileName, const std::string & strNewFileName, bool closed );
    void On_FileRemoved(const std::string & strFileName, bool closed);
    void On_FileAdded(const std::string & strFileName, bool closed);
	void On_DirAdded( const std::string & strFileName, bool closed );
    void On_FileModified(const std::string & strFileName, bool closed);
	void On_FileOpen(const std::string & strFileName);
	void On_ResetAll(const std::string &vol);
	void On_DirRemoved(const std::string & strDirName, bool closed);
	void Commit(const std::vector<IChangeJournalListener::SSequence>& sequences);

	void OnDirMod(const std::string &dir);
	void OnDirRm(const std::string &dir);

	static void update(void);
	static void update_and_wait(std::vector<std::string>& r_open_files);

	static void freeze(void);
	static void unfreeze(void);

	bool is_stopped(void);

	static void update_last_backup_time(void);
	static void commit_last_backup_time(void);

	static _i64 get_current_filetime();

	IDatabase* getDatabase()
	{
		return db;
	}

private:
	static IPipe *pipe;
	IDatabase *db;

	volatile bool do_stop;
	
	IQuery* q_add_dir;
	IQuery* q_add_del_dir;
	IQuery* q_update_last_backup_time;

	std::list<SLastEntries> lastentries;
	std::vector<std::string> watching;

	static IMutex *update_mutex;
	static ICondition *update_cond;

	int64 last_backup_filetime;

	std::auto_ptr<ContinuousWatchEnqueue> continuous_watch;

	static std::vector<std::string> open_files;
};