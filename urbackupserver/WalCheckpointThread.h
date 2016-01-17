#pragma once
#include "../Interface/Thread.h"
#include "../Interface/Types.h"

class WalCheckpointThread : public IThread
{
public:
	WalCheckpointThread();

	void checkpoint();

	void operator()();

private:

	void passive_checkpoint();

	int64 last_checkpoint_wal_size;
};