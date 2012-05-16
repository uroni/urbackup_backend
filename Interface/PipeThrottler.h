#ifndef IPIPETHROTTLER_H
#define IPIPETHROTTLER_H

#include "Object.h"
#include <string>

class IPipeThrottler : public IObject
{
public:
	virtual void addBytes(size_t n_bytes)=0;
	virtual void changeThrottleLimit(size_t bps)=0;
};


#endif //IPIPETHROTTLER_H