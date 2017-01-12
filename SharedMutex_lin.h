#pragma once

#include <pthread.h>
#include "Interface/SharedMutex.h"
#include <memory>

#if defined(_DEBUG) || (!defined(_WIN32) && !defined(NDEBUG))
#define SHARED_MUTEX_CHECK
#define SHARED_MUTEX_CHECK_P(x) ,x
#include <set>
#else
#define SHARED_MUTEX_CHECK_P(x)
#endif

class SharedMutex : public ISharedMutex
{
public:
	SharedMutex();
	~SharedMutex();

	virtual ILock* readLock();

	virtual ILock* writeLock();
	
#ifdef SHARED_MUTEX_CHECK
	void rmLockCheck();
#endif

private:
	pthread_rwlock_t lock;
	
#ifdef SHARED_MUTEX_CHECK
	std::auto_ptr<IMutex> check_mutex;
	std::set<pthread_t> check_threads;
#endif
};

class ReadLock : public ILock
{
public:
	ReadLock(pthread_rwlock_t* read_lock
			SHARED_MUTEX_CHECK_P(SharedMutex* shared_mutex));
	~ReadLock();

private:
	pthread_rwlock_t* read_lock;
#ifdef SHARED_MUTEX_CHECK
	SharedMutex* shared_mutex;
#endif
};

class WriteLock : public ILock
{
public:
	WriteLock(pthread_rwlock_t* write_lock);
	~WriteLock();

private:
	pthread_rwlock_t* write_lock;
};