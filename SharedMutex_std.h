#pragma once

#include <shared_mutex>

#include "Interface/SharedMutex.h"
#include <memory>

class SharedMutex : public ISharedMutex
{
public:
	virtual ILock* readLock();

	virtual ILock* writeLock();

private:
	std::shared_timed_mutex mutex;
};

class ReadLock : public ILock
{
public:
	ReadLock(std::shared_lock<std::shared_timed_mutex>* read_lock);

private:
	std::auto_ptr<std::shared_lock<std::shared_timed_mutex> > read_lock;
};

class WriteLock : public ILock
{
public:
	WriteLock(std::unique_lock<std::shared_timed_mutex>* write_lock);

private:
	std::auto_ptr<std::unique_lock<std::shared_timed_mutex> > write_lock;
};