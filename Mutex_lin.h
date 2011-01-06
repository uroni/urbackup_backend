#include "Interface/Mutex.h"

#include <pthread.h>

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
	pthread_mutex_t ptmutex;
};

class CLock : public ILock
{
public:
	CLock(pthread_mutex_t *ptmutex);
	~CLock();
	pthread_mutex_t * getLock();
private:

	pthread_mutex_t *lock;
};

