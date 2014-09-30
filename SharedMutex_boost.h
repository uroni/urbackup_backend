#pragma once

#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/locks.hpp>

#include "Interface/SharedMutex.h"
#include <memory>

class SharedMutex : public ISharedMutex
{
public:
	virtual ILock* readLock();

	virtual ILock* writeLock();

private:
	boost::shared_mutex mutex;
};

class ReadLock : public ILock
{
public:
	ReadLock(boost::shared_lock<boost::shared_mutex>* read_lock);

private:
	std::auto_ptr<boost::shared_lock<boost::shared_mutex> > read_lock;
};

class WriteLock : public ILock
{
public:
	WriteLock(boost::unique_lock<boost::shared_mutex>* write_lock);

private:
	std::auto_ptr<boost::unique_lock<boost::shared_mutex> > write_lock;
};