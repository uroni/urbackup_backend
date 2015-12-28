#pragma once
#include "../Interface/Thread.h"

class WalCheckpointThread : public IThread
{
public:

	void checkpoint();

	void operator()();
};