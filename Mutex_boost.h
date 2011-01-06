#include "Interface/Mutex.h"

#include <boost/thread/recursive_mutex.hpp>

class CMutex : public IMutex
{
public:
	CMutex(void);
	~CMutex(void);
	virtual void Lock(void);
	virtual ILock * Lock2(void);
	virtual void Unlock(void);
	virtual bool TryLock(void);

private:
	boost::recursive_mutex mutex;
	boost::recursive_mutex::scoped_lock *lock;
};

class CLock : public ILock
{
public:
	CLock(boost::recursive_mutex::scoped_lock *pLock);
	~CLock();
	boost::recursive_mutex::scoped_lock * getLock();
private:

	boost::recursive_mutex::scoped_lock * lock;
};