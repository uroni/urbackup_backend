#pragma once

#include <shared_mutex>

#include "Interface/SharedMutex.h"
#include <memory>

#if defined(_DEBUG) || (!defined(_WIN32) && !defined(NDEBUG))
#define SHARED_MUTEX_CHECK
#define SHARED_MUTEX_CHECK_P(x) ,x
#include <mutex>
#include <set>
#include <thread>
#else
#define SHARED_MUTEX_CHECK_P(x)
#endif

class SharedMutex : public ISharedMutex
{
public:
	virtual ILock* readLock();

	virtual ILock* writeLock();

#ifdef SHARED_MUTEX_CHECK
	void rmLockCheck();
#endif

private:
	std::shared_timed_mutex mutex;

#ifdef SHARED_MUTEX_CHECK
	std::mutex check_mutex;
	std::set<std::thread::id> check_threads;
#endif
};

class ReadLock : public ILock
{
public:
	ReadLock(std::shared_lock<std::shared_timed_mutex>* read_lock
		SHARED_MUTEX_CHECK_P(SharedMutex* shared_mutex));

#ifdef SHARED_MUTEX_CHECK
	~ReadLock() {
		shared_mutex->rmLockCheck();
	}
#endif

private:
	std::auto_ptr<std::shared_lock<std::shared_timed_mutex> > read_lock;

#ifdef SHARED_MUTEX_CHECK
	SharedMutex* shared_mutex;
#endif
};

class WriteLock : public ILock
{
public:
	WriteLock(std::unique_lock<std::shared_timed_mutex>* write_lock);

private:
	std::auto_ptr<std::unique_lock<std::shared_timed_mutex> > write_lock;
};