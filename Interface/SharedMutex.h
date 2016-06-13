#pragma once

#include "Mutex.h"

class ISharedMutex : public IObject
{
public:
	virtual ILock* readLock()=0;

	virtual ILock* writeLock()=0;
};

class IScopedWriteLock
{
public:
	IScopedWriteLock(ISharedMutex *pMutex){ if(pMutex!=NULL)lock=pMutex->writeLock();else lock=NULL; }
	~IScopedWriteLock(){ if(lock!=NULL) lock->Remove(); }
	void relock(ISharedMutex *pMutex){ if(lock!=NULL) lock->Remove(); if(pMutex!=NULL)lock=pMutex->writeLock();else lock=NULL; }
	ILock * getLock(){ return lock; }

private:
	ILock *lock;
};

class IScopedReadLock
{
public:
	IScopedReadLock(ISharedMutex *pMutex){ if(pMutex!=NULL)lock=pMutex->readLock();else lock=NULL; }
	~IScopedReadLock(){ if(lock!=NULL) lock->Remove(); }
	void relock(ISharedMutex *pMutex){ if(lock!=NULL) lock->Remove(); if(pMutex!=NULL)lock=pMutex->readLock();else lock=NULL; }
	ILock * getLock(){ return lock; }

private:
	ILock *lock;
};