#include "Interface/Mutex.h"

#include <mutex>

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
	std::recursive_mutex mutex;
	std::unique_lock<std::recursive_mutex> *lock;
};

class CLock : public ILock
{
public:
	CLock(std::unique_lock<std::recursive_mutex> *pLock);
	~CLock();

	std::unique_lock<std::recursive_mutex>* getLock();
private:

	std::unique_lock<std::recursive_mutex>* lock;
};