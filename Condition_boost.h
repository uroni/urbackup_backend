#include "Interface/Condition.h"

#include <boost/thread/condition.hpp>
#include <boost/thread/xtime.hpp>

class CCondition : public ICondition
{
public:
	CCondition(void){}
	virtual void wait(IScopedLock *lock, int timems=-1);
	virtual void notify_one(void);
	virtual void notify_all(void);
	
	static boost::xtime getWaitTime(int timeoutms);

private:
	boost::condition cond;
};