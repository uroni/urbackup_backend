#include "Interface/Condition.h"

#include <condition_variable>
#include <chrono>

class CCondition : public ICondition
{
public:
	CCondition(void){}
	virtual void wait(IScopedLock *lock, int timems=-1);
	virtual void notify_one();
	virtual void notify_all();

private:
	std::condition_variable_any cond;
};