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

#include "MemoryPipe.h"
#include "Server.h"
#ifndef _WIN32
#include <memory.h>
#endif

CMemoryPipe::CMemoryPipe(void)
	: has_error(false)
{
    mutex=Server->createMutex();
    cond=Server->createCondition();
}

CMemoryPipe::~CMemoryPipe(void)
{
    Server->destroy(mutex);
    Server->destroy(cond);
}

size_t CMemoryPipe::Read(char *buffer, size_t bsize, int timeoutms)
{
	IScopedLock lock(mutex);
	if( timeoutms>0 )
	{
		int64 starttime=Server->getTimeMS();
		int64 currtime=starttime;
		while( queue.empty() && starttime+timeoutms>currtime && !has_error)
		{
			cond->wait( &lock, timeoutms- static_cast<int>(currtime-starttime) );
			if(queue.empty())
			{
				currtime=Server->getTimeMS();
			}
		}

		if(queue.empty())
			return 0;
	}	
	else if( timeoutms==0 )
	{
		if( queue.size()==0 )
			return 0;
	}
	else
	{
		while( queue.size()==0 && !has_error )
		{
			cond->wait(&lock);		
		}

		if(has_error)
		{
			return 0;
		}
	}
	
	std::string *cstr=&queue[0];
	
	size_t psize=cstr->size();
	
	if( psize<=bsize )
	{
		memcpy( buffer, cstr->c_str(), psize );
		queue.erase( queue.begin() );
		return psize;
	}
	else
	{
		memcpy( buffer, cstr->c_str(), bsize );
		cstr->erase(0, bsize );
		return bsize;
	}
}

bool CMemoryPipe::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	IScopedLock lock(mutex);
	
	queue.push_back("");
	std::deque<std::string>::iterator iter=queue.end();
	--iter;
	std::string *nstr=&(*iter);
	
	nstr->resize( bsize );
	memcpy( (char*)nstr->c_str(), buffer, bsize );
	
	cond->notify_one();
	
	return true;
}

size_t CMemoryPipe::Read(std::string *str, int timeoutms )
{
	IScopedLock lock(mutex);
	
	if( timeoutms>0 )
	{
		int64 starttime=Server->getTimeMS();
		int64 currtime=starttime;
		while( queue.empty() && starttime+timeoutms>currtime && !has_error)
		{
			cond->wait( &lock, timeoutms- static_cast<int>(currtime-starttime) );
			if(queue.empty())
			{
				currtime=Server->getTimeMS();
			}
		}

		if(queue.empty())
			return 0;
	}	
	else if( timeoutms==0 )
	{
		if( queue.size()==0 )
			return 0;
	}
	else
	{
		while( queue.size()==0 && !has_error )
		{
			cond->wait(&lock);		
		}

		if(has_error)
		{
			return 0;
		}
	}
	
	std::string *fs=&queue[0];
	
	size_t fsize=fs->size();
	
	str->resize( fsize );
	 
	memcpy( (char*) str->c_str(), fs->c_str(), fsize );
	
	queue.erase( queue.begin() );
	
	return fsize;		
}

bool CMemoryPipe::Write(const std::string &str, int timeoutms, bool flush)
{
	IScopedLock lock(mutex);
	
	queue.push_back( str );
	
	cond->notify_one();
	
	return true;
}

bool CMemoryPipe::isWritable(int timeoutms)
{
	return true;
}

bool CMemoryPipe::isReadable(int timeoutms)
{
	IScopedLock lock(mutex);

	if( queue.size()>0 )
		return true;
	
	if(timeoutms>0)
		cond->wait( &lock, timeoutms );
	else if(timeoutms<0)
		cond->wait(&lock);


	if( queue.size()>0 )
		return true;
	else
		return false;
}

bool CMemoryPipe::hasError(void)
{
	IScopedLock lock(mutex);
	return has_error;
}

size_t CMemoryPipe::getNumElements(void)
{
	IScopedLock lock(mutex);
	return queue.size();
}

void CMemoryPipe::shutdown(void)
{
	IScopedLock lock(mutex);
	has_error=true;
	cond->notify_all();
}

void CMemoryPipe::addThrottler(IPipeThrottler *throttler)
{
	
}

void CMemoryPipe::addOutgoingThrottler(IPipeThrottler *throttler)
{
	
}

void CMemoryPipe::addIncomingThrottler(IPipeThrottler *throttler)
{
	
}

_i64 CMemoryPipe::getTransferedBytes(void)
{
	return 0;
}

void CMemoryPipe::resetTransferedBytes(void)
{
}

bool CMemoryPipe::Flush( int timeoutms/*=-1 */ )
{
	return true;
}
