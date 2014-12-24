#pragma once

#include "Interface/PipeThrottler.h"
#include <memory>

class IMutex;

class PipeThrottler : public IPipeThrottler
{
public:
	PipeThrottler(size_t bps, IPipeThrottlerUpdater* updater);
	~PipeThrottler(void);

	virtual bool addBytes(size_t new_bytes, bool wait);

	virtual void changeThrottleLimit(size_t bps);

	virtual void changeThrottleUpdater(IPipeThrottlerUpdater* new_updater);

private:
	size_t throttle_bps;
	int64 update_time_interval;
	size_t curr_bytes;
	int64 lastresettime;
	int64 lastupdatetime;
	std::auto_ptr<IPipeThrottlerUpdater> updater;

	IMutex *mutex;
};