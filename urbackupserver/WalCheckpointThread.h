#pragma once
#include "../Interface/Thread.h"
#include "../Interface/Types.h"

class WalCheckpointThread : public IThread
{
public:
	WalCheckpointThread(int64 passive_checkpoint_size, int64 full_checkpoint_size, const std::string& db_fn, DATABASE_ID db_id);

	void checkpoint();

	void operator()();

private:

	void sync_database();

	void passive_checkpoint();

	int64 last_checkpoint_wal_size;

	int64 passive_checkpoint_size;
	int64 full_checkpoint_size;
	std::string db_fn;
	DATABASE_ID db_id;
};