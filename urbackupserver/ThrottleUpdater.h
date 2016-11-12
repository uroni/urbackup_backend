#pragma once
#include "../Interface/PipeThrottler.h"

enum ThrottleScope
{
	ThrottleScope_GlobalLocal,
	ThrottleScope_GlobalInternet,
	ThrottleScope_Local,
	ThrottleScope_Internet
};

class ThrottleUpdater : public IPipeThrottlerUpdater
{
public:
	ThrottleUpdater(int clientid, ThrottleScope throttle_scope);

	virtual int64 getUpdateIntervalMs();

	virtual size_t getThrottleLimit(bool& percent_max);

private:
	int clientid;
	ThrottleScope throttle_scope;
};