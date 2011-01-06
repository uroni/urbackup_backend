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

CMutex::CMutex(void)
{
	pthread_mutexattr_t attr;
	if(pthread_mutexattr_init(&attr)!=0)
	{
		Server->Log("Error initializing mutexattr", LL_ERROR);
	}
	if(pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE)!=0)
	{
		Server->Log("Error setting PTHREAD_MUTEX_RECURSIVE", LL_ERROR);
	}
	if(pthread_mutex_init(&ptmutex, &attr)!=0)
	{
		Server->Log("Error initializing mutex", LL_ERROR);
	}
	if(pthread_mutexattr_destroy(&attr)!=0)
	{
		Server->Log("Error destroing mutexattr", LL_ERROR);
	}
}

CMutex::~CMutex(void)
{
	if(pthread_mutex_destroy(&ptmutex)!=0)
	{
		Server->Log("Error destroying mutex", LL_ERROR);
	}
}

void CMutex::Lock(void)
{
	if(pthread_mutex_lock( &ptmutex )!=0)
	{
		Server->Log("Error locking mutex", LL_ERROR);
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
	if(pthread_mutex_unlock( &ptmutex )!=0)
	{
		Server->Log("Error unlocking mutex", LL_ERROR);
	}
}

CLock::CLock(pthread_mutex_t *ptmutex)
{
	if(pthread_mutex_lock(ptmutex)!=0)
	{
		Server->Log("Error locking mutex -2", LL_ERROR);
	}
	lock=ptmutex;
}

CLock::~CLock()
{
	if(pthread_mutex_unlock(lock)!=0)
	{
		Server->Log("Error unlocking mutex -2", LL_ERROR);
	}
}

pthread_mutex_t * CLock::getLock()
{
	return lock;
}

