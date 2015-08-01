/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "Mutex_boost.h"

CMutex::CMutex(void)
{
	lock=NULL;
}

CMutex::~CMutex(void)
{
}

void CMutex::Lock(void)
{
	boost::recursive_mutex::scoped_lock *n_lock=new boost::recursive_mutex::scoped_lock(mutex);
	lock=n_lock;
}

bool CMutex::TryLock(void)
{
	boost::recursive_mutex::scoped_try_lock *n_lock=new boost::recursive_mutex::scoped_try_lock(mutex);
	if(n_lock->owns_lock())
	{
		lock=(boost::recursive_mutex::scoped_lock*)n_lock;
		return true;
	}
	else
	{
		return false;
	}
}

ILock* CMutex::Lock2(void)
{
	return new CLock(new boost::recursive_mutex::scoped_lock(mutex));
}

void CMutex::Unlock(void)
{
	delete lock;
}

CLock::CLock(boost::recursive_mutex::scoped_lock *pLock)
{
	lock=pLock;
}
CLock::~CLock()
{
	delete lock;
}

boost::recursive_mutex::scoped_lock * CLock::getLock()
{
	return lock;
}

