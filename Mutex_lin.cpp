/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "Mutex_lin.h"
#include "Server.h"
#include "stringtools.h"
#include <assert.h>

CMutex::CMutex(void)
{
	pthread_mutexattr_t attr;
	int rc;
	if((rc=pthread_mutexattr_init(&attr))!=0)
	{
		Server->Log("Error initializing mutexattr rc="+convert(rc), LL_ERROR);
		assert(false);
	}
	if((rc=pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE))!=0)
	{
		Server->Log("Error setting PTHREAD_MUTEX_RECURSIVE rc="+convert(rc), LL_ERROR);
		assert(false);
	}
	if((rc=pthread_mutex_init(&ptmutex, &attr))!=0)
	{
		Server->Log("Error initializing mutex rc="+convert(rc), LL_ERROR);
		assert(false);
	}
	if((rc=pthread_mutexattr_destroy(&attr))!=0)
	{
		Server->Log("Error destroing mutexattr rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

CMutex::~CMutex(void)
{
	int rc;
	if( (rc=pthread_mutex_destroy(&ptmutex))!=0)
	{
		Server->Log("Error destroying mutex rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

void CMutex::Lock(void)
{
	int rc;
	if((rc=pthread_mutex_lock( &ptmutex ))!=0)
	{
		Server->Log("Error locking mutex rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

bool CMutex::TryLock(void)
{
	return pthread_mutex_trylock( &ptmutex )==0;
}

ILock * CMutex::Lock2(void)
{
	return new CLock(&ptmutex);
}

void CMutex::Unlock(void)
{
	int rc;
	if((rc=pthread_mutex_unlock( &ptmutex ))!=0)
	{
		Server->Log("Error unlocking mutex rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

CLock::CLock(pthread_mutex_t *ptmutex)
{
	int rc;
	if((rc=pthread_mutex_lock(ptmutex))!=0)
	{
		Server->Log("Error locking mutex -2 rc="+convert(rc), LL_ERROR);
		assert(false);
	}
	lock=ptmutex;
}

CLock::~CLock()
{
	int rc;
	if((rc=pthread_mutex_unlock(lock))!=0)
	{
		Server->Log("Error unlocking mutex -2 rc="+convert(rc), LL_ERROR);
		assert(false);
	}
}

pthread_mutex_t * CLock::getLock()
{
	return lock;
}

