#pragma once
#include "../Interface/Thread.h"

class WalCheckpointThread : public IThread
{
public:

	void checkpoint(bool in_transaction=false);

	void operator()();
};