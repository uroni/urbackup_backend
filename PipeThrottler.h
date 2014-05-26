#include "Interface/PipeThrottler.h"

class IMutex;

class PipeThrottler : public IPipeThrottler
{
public:
	PipeThrottler(size_t bps);
	~PipeThrottler(void);

	virtual bool addBytes(size_t new_bytes, bool wait);

	virtual void changeThrottleLimit(size_t bps);

private:
	size_t throttle_bps;
	size_t curr_bytes;
	int64 lastresettime;

	IMutex *mutex;
};