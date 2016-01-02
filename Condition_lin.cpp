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

#include "Condition_lin.h"
#include "Mutex_lin.h"
#ifndef _WIN32
#include <sys/time.h>
#endif
#include <assert.h>

#include "Server.h"
#include "stringtools.h"

CCondition::CCondition()
{
	pthread_cond_init(&cond, NULL);
}

CCondition::~CCondition()
{
	int rc=pthread_cond_destroy(&cond);
	if(rc!=0)
	{
		Server->Log("Destroying condition failed ec="+convert(rc), LL_ERROR);
	}
}

void CCondition::wait(IScopedLock *lock, int timems)
{
	CLock* clock = (CLock*)lock->getLock();
	assert(clock);
	pthread_mutex_t *ptmutex=clock->getLock();
	if(timems<0)
	{
		pthread_cond_wait(&cond, ptmutex);
	}
	else
	{
		timeval tp;
		gettimeofday(&tp, NULL);
		timespec t;
		t.tv_sec=tp.tv_sec+timems/(int)1000;
		t.tv_nsec=tp.tv_usec+(timems%1000)*1000000;
		pthread_cond_timedwait(&cond, ptmutex, &t); 
	}
}

void CCondition::notify_one(void)
{
	pthread_cond_signal(&cond);
}

void CCondition::notify_all(void)
{
	pthread_cond_broadcast(&cond);
}

