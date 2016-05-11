/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "SharedMutex_lin.h"
#include "Server.h"
#include "stringtools.h"
#include <assert.h>

SharedMutex::SharedMutex()
{
	pthread_rwlockattr_t attr;
	int rc;
	if((rc=pthread_rwlockattr_init(&attr))!=0)
	{
		Server->Log("Error initializing rwlockattr rc="+convert(rc), LL_ERROR);
		assert(false);
	}
#ifdef __GLIBC__
	if((rc=pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP))!=0)
	{
		Server->Log("Error setting PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP rc="+convert(rc), LL_ERROR);
		assert(false);
	}
#endif
	rc = pthread_rwlock_init(&lock, &attr);
	if(rc)
	{
		Server->Log("Error initializing rwlock rc="+convert(rc), LL_ERROR);
		assert(false);
	}
	if((rc=pthread_rwlockattr_destroy(&attr))!=0)
	{
		Server->Log("Error destroing rwlockattr rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

SharedMutex::~SharedMutex()
{
	int rc = pthread_rwlock_destroy(&lock);
	if(rc)
	{
		Server->Log("Error destroying rwlock rc="+convert(rc), LL_ERROR);
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
		Server->Log("Error locking rwlock_rdlock rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

ReadLock::~ReadLock()
{
	int rc = pthread_rwlock_unlock(read_lock);
	if(rc)
	{
		Server->Log("Error unlocking rwlock rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

WriteLock::WriteLock( pthread_rwlock_t* write_lock )
	: write_lock(write_lock)
{
	int rc = pthread_rwlock_wrlock(write_lock);
	if(rc)
	{
		Server->Log("Error locking rwlock_wrlock rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

WriteLock::~WriteLock()
{
	int rc = pthread_rwlock_unlock(write_lock);
	if(rc)
	{
		Server->Log("Error unlocking rwlock rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}
