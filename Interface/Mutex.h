#ifndef IMUTEX_H
#define IMUTEX_H

#include "Object.h"
#include "Types.h"

class ILock : public IObject
{
};

class IMutex : public IObject
{
public:
	virtual void Lock(void)=0;
	virtual ILock * Lock2(void)=0;
	virtual void Unlock(void)=0;
	virtual bool TryLock(void)=0;
};

class IScopedLock
{
public:
	IScopedLock(IMutex *pMutex){ if(pMutex!=NULL)lock=pMutex->Lock2();else lock=NULL; }
	~IScopedLock(){ if(lock!=NULL) lock->Remove(); }
	void relock(IMutex *pMutex){ if(lock!=NULL) lock->Remove(); if(pMutex!=NULL)lock=pMutex->Lock2();else lock=NULL; }
	ILock * getLock(){ return lock; }

private:
	ILock *lock;
};

#endif //IMUTEX_H