#pragma once
#include "../Interface/Thread.h"
#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include <set>

class WalCheckpointThread : public IThread
{
public:
	WalCheckpointThread(int64 passive_checkpoint_size, int64 full_checkpoint_size, const std::string& db_fn, DATABASE_ID db_id);

	void checkpoint();

	void operator()();

	static void lockForBackup(const std::string& fn);
	static void unlockForBackup(const std::string& fn);

	static void init_mutex();
	static void destroy_mutex();

private:

	void waitAndLockForBackup();

	void sync_database();

	void passive_checkpoint();

	int64 last_checkpoint_wal_size;

	int64 passive_checkpoint_size;
	int64 full_checkpoint_size;
	std::string db_fn;
	DATABASE_ID db_id;

	bool cannot_open;

	static IMutex* mutex;
	static ICondition* cond;
	static std::set<std::string> tolock_dbs;
	static std::set<std::string> locked_dbs;
};