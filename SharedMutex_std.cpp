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

#include "SharedMutex_std.h"
#ifdef SHARED_MUTEX_CHECK
#include <assert.h>
#endif

ILock* SharedMutex::readLock()
{
#ifdef SHARED_MUTEX_CHECK
	{
		std::unique_lock<std::mutex> n_lock(check_mutex);
		std::pair<std::set<std::thread::id>::iterator, bool> ins = check_threads.insert(std::this_thread::get_id());
		assert(ins.second);
	}
#endif
	return new ReadLock(new std::shared_lock<std::shared_timed_mutex>(mutex)
		SHARED_MUTEX_CHECK_P(this));
}

ILock* SharedMutex::writeLock()
{
	return new WriteLock(new std::unique_lock<std::shared_timed_mutex>(mutex));
}

#ifdef SHARED_MUTEX_CHECK
void SharedMutex::rmLockCheck()
{
	std::unique_lock<std::mutex> n_lock(check_mutex);
	assert(check_threads.erase(std::this_thread::get_id()) == 1);
}
#endif

ReadLock::ReadLock( std::shared_lock<std::shared_timed_mutex>* read_lock 
	SHARED_MUTEX_CHECK_P(SharedMutex* shared_mutex))
	: read_lock(read_lock)
	  SHARED_MUTEX_CHECK_P(shared_mutex(shared_mutex))
{

}

WriteLock::WriteLock( std::unique_lock<std::shared_timed_mutex>* write_lock )
	: write_lock(write_lock)
{

}
