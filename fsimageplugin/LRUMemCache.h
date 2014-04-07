#pragma once

#include "../Interface/Types.h"

#include <vector>


struct SCacheItem
{
	SCacheItem()
		: buffer(NULL), offset(0)
	{
	}

	char* buffer;
	__int64 offset;
};

class LRUMemCache;

class ICacheEvictionCallback
{
private:
	virtual void evictFromLruCache(const SCacheItem& item) = 0;

	friend class LRUMemCache;
};

class LRUMemCache
{
public:
	LRUMemCache(size_t buffersize, size_t nbuffers);
	~LRUMemCache();

	char* get(__int64 offset, size_t& bsize);

	bool put(__int64 offset, const char* buffer, size_t bsize);

	char* create(__int64 offset);

	void setCacheEvictionCallback(ICacheEvictionCallback* cacheEvictionCallback);

	void clear();

private:

	SCacheItem createInt(__int64 offset);

	void putBack(size_t idx);

	void evict(SCacheItem& item, bool deleteBuffer);

	std::vector<SCacheItem> lruItems;

	size_t buffersize;
	size_t nbuffers;

	ICacheEvictionCallback* callback;
};