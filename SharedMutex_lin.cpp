#include "SharedMutex_lin.h"
#include "Server.h"
#include "stringtools.h"
#include <assert.h>

SharedMutex::SharedMutex()
{
	int rc = pthread_rwlock_init(&lock, NULL);
	if(rc)
	{
		Server->Log("Error initializing rwlock rc="+nconvert(rc), LL_ERROR);
		assert(false);
	}
}

SharedMutex::~SharedMutex()
{
	int rc = pthread_rwlock_destroy(&lock);
	if(rc)
	{
		Server->Log("Error destroying rwlock rc="+nconvert(rc), LL_ERROR);
		assert(false);
	}
}

ILock* SharedMutex::readLock()
{
	return new ReadLock(&lock);
}

ILock* SharedMutex::writeLock()
{
	return new WriteLock(&lock);
}


ReadLock::ReadLock( pthread_rwlock_t* read_lock )
	: read_lock(read_lock)
{
	int rc = pthread_rwlock_rdlock(read_lock);
	if(rc)
	{
		Server->Log("Error locking rwlock rc="+nconvert(rc), LL_ERROR);
		assert(false);
	}
}

ReadLock::~ReadLock()
{
	int rc = pthread_rwlock_unlock(read_lock);
	if(rc)
	{
		Server->Log("Error unlocking rwlock rc="+nconvert(rc), LL_ERROR);
		assert(false);
	}
}

WriteLock::WriteLock( pthread_rwlock_t* write_lock )
	: write_lock(write_lock)
{
	int rc = pthread_rwlock_wrlock(write_lock);
	if(rc)
	{
		Server->Log("Error locking rwlock rc="+nconvert(rc), LL_ERROR);
		assert(false);
	}
}

WriteLock::~WriteLock()
{
	int rc = pthread_rwlock_unlock(write_lock);
	if(rc)
	{
		Server->Log("Error unlocking rwlock rc="+nconvert(rc), LL_ERROR);
		assert(false);
	}
}
