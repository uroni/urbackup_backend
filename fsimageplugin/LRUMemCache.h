#pragma once

#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Thread.h"
#include "CompressedFile.h"

#include <vector>
#include <set>
#include <memory>

class LRUMemCache : public IThread
{
public:
	LRUMemCache(size_t buffersize, size_t nbuffers, size_t n_threads);
	~LRUMemCache();

	char* get(__int64 offset, size_t& bsize);

	bool put(__int64 offset, const char* buffer, size_t bsize);

	char* create(__int64 offset);

	void setCacheEvictionCallback(ICacheEvictionCallback* cacheEvictionCallback);

	void clear();

	void operator()();

private:
	void finishThreads();
	void waitThreadWork();
	//Waits until the item at offset is not being evicted by the threads anymore
	void waitEvicted(__int64 offset);

	SCacheItem createInt(__int64 offset);

	void putBack(size_t idx);

	char* evict(SCacheItem& item, bool deleteBuffer);

	char* getLruItemBuffer(IScopedLock& lock);

	std::vector<SCacheItem> lruItems;
	std::vector<SCacheItem> evictedItems;
	//Offsets of evicted items queued for or being processed by the threads
	std::set<__int64> evictingOffsets;
	std::vector<char*> lruItemBuffers;

	std::auto_ptr<IMutex> mutex;
	std::auto_ptr<ICondition> cond;
	std::auto_ptr<ICondition> cond_wait;

	size_t buffersize;
	size_t nbuffers;
	size_t n_threads;
	size_t n_threads_working;
	bool wait_work;
	bool do_quit;

	ICacheEvictionCallback* callback;
};