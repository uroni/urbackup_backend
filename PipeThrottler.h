#include "Interface/PipeThrottler.h"

class IMutex;

class PipeThrottler : public IPipeThrottler
{
public:
	PipeThrottler(size_t bps, int64 update_time_interval, IPipeThrottlerUpdater* updater, void* userdata);
	~PipeThrottler(void);

	virtual bool addBytes(size_t new_bytes, bool wait);

	virtual void changeThrottleLimit(size_t bps);

private:
	size_t throttle_bps;
	int64 update_time_interval;
	size_t curr_bytes;
	int64 lastresettime;
	int64 lastupdatetime;
	IPipeThrottlerUpdater* updater;
	void* userdata;

	IMutex *mutex;
};