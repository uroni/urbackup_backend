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

#include "Mutex_std.h"

CMutex::CMutex(void)
{
	lock=NULL;
}

CMutex::~CMutex(void)
{
}

void CMutex::Lock(void)
{
	std::unique_lock<std::recursive_mutex> *n_lock=new std::unique_lock<std::recursive_mutex>(mutex);
	lock=n_lock;
}

bool CMutex::TryLock(void)
{
	if (mutex.try_lock())
	{
		lock = new std::unique_lock<std::recursive_mutex>(mutex, std::adopt_lock);
		return true;
	}
	else
	{
		return false;
	}
}

ILock* CMutex::Lock2(void)
{
	return new CLock(new std::unique_lock<std::recursive_mutex>(mutex));
}

void CMutex::Unlock(void)
{
	delete lock;
}

CLock::CLock(std::unique_lock<std::recursive_mutex>* pLock)
{
	lock=pLock;
}
CLock::~CLock()
{
	delete lock;
}

std::unique_lock<std::recursive_mutex>* CLock::getLock()
{
	return lock;
}

