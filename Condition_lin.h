#include "Interface/Condition.h"

#include <pthread.h>

class CCondition : public ICondition
{
public:
	CCondition();
	~CCondition();
	virtual void wait(IScopedLock *lock, int timems=-1);
	virtual void notify_one(void);
	virtual void notify_all(void);

private:
	pthread_cond_t cond;
};
