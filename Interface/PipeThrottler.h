#ifndef IPIPETHROTTLER_H
#define IPIPETHROTTLER_H

#include "Object.h"
#include <string>

class IPipeThrottlerUpdater : public IObject
{
public:
	virtual size_t getThrottleLimit(void* userdata)=0;
};

class IPipeThrottler : public IObject
{
public:
	virtual bool addBytes(size_t n_bytes, bool wait)=0;
	virtual void changeThrottleLimit(size_t bps)=0;
};


#endif //IPIPETHROTTLER_H