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

#include "LRUMemCache.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <string.h>
#include <assert.h>


LRUMemCache::LRUMemCache(size_t buffersize, size_t nbuffers, size_t p_n_threads)
	: buffersize(buffersize), nbuffers(nbuffers), callback(nullptr),
	mutex(Server->createMutex()), cond(Server->createCondition()), n_threads(p_n_threads),
	do_quit(false), n_threads_working(0), cond_wait(Server->createCondition()), wait_work(false)
{
	if (n_threads > 0)
		--n_threads;

	for (size_t i = 0; i < n_threads; ++i)
	{
		Server->createThread(this, "comp img"+convert(i));
	}
}

char* LRUMemCache::get( __int64 offset, size_t& bsize )
{
	for(size_t i=lruItems.size(); i-->0; )
	{
		if(lruItems[i].offset<=offset &&
			lruItems[i].offset+static_cast<__int64>(buffersize)>offset)
		{
			size_t innerOffset = static_cast<size_t>(offset-lruItems[i].offset);
			bsize = buffersize - innerOffset;
			return lruItems[i].buffer + innerOffset;
		}
	}

	return nullptr;
}

bool LRUMemCache::put( __int64 offset, const char* buffer, size_t bsize )
{
	for(size_t i=lruItems.size(); i-->0; )
	{
		if(lruItems[i].offset<=offset &&
			lruItems[i].offset+static_cast<__int64>(buffersize)>offset)
		{
			size_t innerOffset = static_cast<size_t>(offset-lruItems[i].offset);

			if( buffersize - innerOffset < bsize)
			{
				return false;
			}

			memcpy(lruItems[i].buffer + innerOffset, buffer, bsize);

			putBack(i);

			return true;
		}
	}

	SCacheItem newItem = createInt(offset);

	size_t innerOffset = static_cast<size_t>(offset-newItem.offset);

	if( buffersize - innerOffset < bsize)
	{
		return false;
	}

	memcpy(newItem.buffer + innerOffset, buffer, bsize);

	return true;
}

void LRUMemCache::putBack( size_t idx )
{
	if(idx == lruItems.size()-1)
		return;

	SCacheItem item = lruItems[idx];
	lruItems.erase(lruItems.begin()+idx);
	lruItems.push_back(item);
}

void LRUMemCache::setCacheEvictionCallback( ICacheEvictionCallback* cacheEvictionCallback )
{
	callback=cacheEvictionCallback;
}

void LRUMemCache::clear()
{
	waitThreadWork();

	for(size_t i=0;i<lruItems.size();++i)
	{
		evict(lruItems[i], true);
	}
	lruItems.clear();
}

void LRUMemCache::operator()()
{
	IScopedLock lock(mutex.get());
	while (!do_quit)
	{
		while (!do_quit
			&& evictedItems.empty())
		{
			cond->wait(&lock);
		}

		if (evictedItems.empty())
			break;

		SCacheItem item = evictedItems.back();
		evictedItems.pop_back();
		++n_threads_working;
		lock.relock(nullptr);

		callback->evictFromLruCache(item);

		lock.relock(mutex.get());
		lruItemBuffers.push_back(item.buffer);
		--n_threads_working;
		if(wait_work)
			cond_wait->notify_all();
	}	
	--n_threads;
	cond_wait->notify_all();
}

char* LRUMemCache::evict( SCacheItem& item, bool deleteBuffer )
{
	if (deleteBuffer)
	{
		if (callback != nullptr)
		{
			callback->evictFromLruCache(item);
		}
		if (deleteBuffer)
		{
			delete[] item.buffer;
		}

		return nullptr;
	}
	else
	{
		if (callback == nullptr)
		{
			return nullptr;
		}

		IScopedLock lock(mutex.get());
		if (evictedItems.size() >= n_threads)
		{
			char* ret = item.buffer;
			callback->evictFromLruCache(item);
			return ret;
		}
		else
		{
			evictedItems.push_back(item);
			cond->notify_one();
			return getLruItemBuffer(lock);
		}
	}
}

char* LRUMemCache::getLruItemBuffer(IScopedLock& lock)
{
	if (!lruItemBuffers.empty())
	{
		char* ret = lruItemBuffers.back();
		lruItemBuffers.pop_back();
		return ret;
	}
	lock.relock(nullptr);
	return new char[buffersize];
}

LRUMemCache::~LRUMemCache()
{
	clear();

	finishThreads();

	for (size_t i = 0; i < lruItemBuffers.size(); ++i)
	{
		delete[] lruItemBuffers[i];
	}

	lruItemBuffers.clear();
}

void LRUMemCache::finishThreads()
{
	IScopedLock lock(mutex.get());
	do_quit = true;
	while (n_threads > 0)
	{
		cond->notify_all();
		cond_wait->wait(&lock);
	}
}

void LRUMemCache::waitThreadWork()
{
	IScopedLock lock(mutex.get());
	wait_work = true;
	while (!evictedItems.empty()
		|| n_threads_working > 0)
	{
		cond_wait->wait(&lock);
	}
	wait_work = false;
}

SCacheItem LRUMemCache::createInt( __int64 offset )
{
	char* buffer=nullptr;
	if(lruItems.size()>=nbuffers)
	{
		SCacheItem& toremove = lruItems[0];
		buffer = evict(toremove, false);
		lruItems.erase(lruItems.begin());
	}
	else
	{
		buffer = new char[buffersize];
	}

	SCacheItem newItem;
	assert(buffer != nullptr);
	newItem.buffer=buffer;
	newItem.offset=offset - offset % buffersize;

	lruItems.push_back(newItem);

	return newItem;
}

char* LRUMemCache::create( __int64 offset )
{
	size_t bsize;
	char* buf = get(offset, bsize);

	if(buf!=nullptr)
	{
		return buf;
	}

	return createInt(offset).buffer;
}
