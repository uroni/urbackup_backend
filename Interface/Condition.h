#ifndef ICONDITION_H
#define ICONDITION_H

#include "Mutex.h"
#include "Object.h"

class ICondition : public IObject
{
public:

	virtual void wait(IScopedLock *lock, int timems=-1)=0;
	virtual void notify_one(void)=0;
	virtual void notify_all(void)=0;
};

#endif //ICONDITION_H