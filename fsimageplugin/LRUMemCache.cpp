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

#include "LRUMemCache.h"
#include <string.h>


LRUMemCache::LRUMemCache( size_t buffersize, size_t nbuffers )
	: buffersize(buffersize), nbuffers(nbuffers), callback(NULL)
{
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

	return NULL;
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
	for(size_t i=0;i<lruItems.size();++i)
	{
		evict(lruItems[i], true);
	}
	lruItems.clear();
}

void LRUMemCache::evict( SCacheItem& item, bool deleteBuffer )
{
	if(callback!=NULL)
	{
		callback->evictFromLruCache(item);
	}
	if(deleteBuffer)
	{
		delete item.buffer;
	}
}

LRUMemCache::~LRUMemCache()
{
	clear();
}

SCacheItem LRUMemCache::createInt( __int64 offset )
{
	char* buffer=NULL;
	if(lruItems.size()==nbuffers)
	{
		SCacheItem& toremove = lruItems[0];
		buffer = toremove.buffer;
		evict(toremove, false);
		lruItems.erase(lruItems.begin());
	}

	SCacheItem newItem;
	if(buffer!=NULL)
	{
		newItem.buffer=buffer;
	}
	else
	{
		newItem.buffer=new char[buffersize];
	}
	newItem.offset=offset - offset % buffersize;

	lruItems.push_back(newItem);

	return newItem;
}

char* LRUMemCache::create( __int64 offset )
{
	size_t bsize;
	char* buf = get(offset, bsize);

	if(buf!=NULL)
	{
		return buf;
	}

	return createInt(offset).buffer;
}
