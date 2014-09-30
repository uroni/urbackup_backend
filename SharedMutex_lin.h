#pragma once

#include <pthread.h>
#include "Interface/SharedMutex.h"
#include <memory>

class SharedMutex : public ISharedMutex
{
public:
	SharedMutex();
	~SharedMutex();

	virtual ILock* readLock();

	virtual ILock* writeLock();

private:
	pthread_rwlock_t lock;
};

class ReadLock : public ILock
{
public:
	ReadLock(pthread_rwlock_t* read_lock);
	~ReadLock();

private:
	pthread_rwlock_t* read_lock;
};

class WriteLock : public ILock
{
public:
	WriteLock(pthread_rwlock_t* write_lock);
	~WriteLock();

private:
	pthread_rwlock_t* write_lock;
};