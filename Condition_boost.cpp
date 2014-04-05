/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "Condition_boost.h"
#include "Mutex_boost.h"
#include "Server.h"

void CCondition::wait(IScopedLock *lock, int timems)
{
	boost::recursive_mutex::scoped_lock *tl=((CLock*)lock->getLock())->getLock();

	if(timems<0)
		cond.wait(*tl);
	else
	{
		cond.timed_wait(*tl, getWaitTime(timems));
	}
}

boost::xtime CCondition::getWaitTime(int timeoutms)
{
	boost::xtime xt;
    xtime_get(&xt, boost::TIME_UTC);
	if( timeoutms>1000 )
	{
		xt.sec+=timeoutms/1000;
		timeoutms=timeoutms%1000;
	}
	xt.nsec+=timeoutms*1000000;
	return xt;
}

void CCondition::notify_one(void)
{
	cond.notify_one();
}

void CCondition::notify_all(void)
{
	cond.notify_all();
}