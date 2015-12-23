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

#include "SharedMutex_std.h"


ILock* SharedMutex::readLock()
{
	return new ReadLock(new std::shared_lock<std::shared_timed_mutex>(mutex));
}

ILock* SharedMutex::writeLock()
{
	return new WriteLock(new std::unique_lock<std::shared_timed_mutex>(mutex));
}


ReadLock::ReadLock( std::shared_lock<std::shared_timed_mutex>* read_lock )
	: read_lock(read_lock)
{

}

WriteLock::WriteLock( std::unique_lock<std::shared_timed_mutex>* write_lock )
	: write_lock(write_lock)
{

}
