#pragma once

#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Thread.h"
#include "CompressedFile.h"

#include <vector>
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

	SCacheItem createInt(__int64 offset);

	void putBack(size_t idx);

	char* evict(SCacheItem& item, bool deleteBuffer);

	char* getLruItemBuffer(IScopedLock& lock);

	std::vector<SCacheItem> lruItems;
	std::vector<SCacheItem> evictedItems;
	std::vector<char*> lruItemBuffers;

	std::unique_ptr<IMutex> mutex;
	std::unique_ptr<ICondition> cond;
	std::unique_ptr<ICondition> cond_wait;

	size_t buffersize;
	size_t nbuffers;
	size_t n_threads;
	size_t n_threads_working;
	bool wait_work;
	bool do_quit;

	ICacheEvictionCallback* callback;
};