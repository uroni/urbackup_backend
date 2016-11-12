#pragma once

#include "Interface/PipeThrottler.h"
#include <memory>

class IMutex;

class PipeThrottler : public IPipeThrottler
{
public:
	PipeThrottler(size_t bps, bool percent_max, IPipeThrottlerUpdater* updater);
	PipeThrottler();
	~PipeThrottler(void);

	virtual bool addBytes(size_t new_bytes, bool wait);

	virtual void changeThrottleLimit(size_t bps, bool p_percent_max);

	virtual void changeThrottleUpdater(IPipeThrottlerUpdater* new_updater);

private:
	enum ThrottleState
	{
		ThrottleState_Probe,
		ThrottleState_Throttle
	};

	size_t throttle_bps;
	bool percent_max;
	int64 update_time_interval;
	size_t curr_bytes;
	int64 lastresettime;
	int64 lastupdatetime;
	std::auto_ptr<IPipeThrottlerUpdater> updater;
	ThrottleState throttle_state;
	int64 lastprobetime;
	float probe_bps;
	size_t throttle_percent;
	float last_probe_result;
	size_t probe_interval;

	IMutex *mutex;
};