#include "FileCache.h"
#include "../Interface/Server.h"

const size_t max_buffer_size=100000;
const unsigned int max_wait_time=10000;
const size_t min_size_no_wait=1000;

std::map<FileCache::SCacheKey, FileCache::SCacheValue> FileCache::cache_buffer;
IMutex *FileCache::mutex=NULL;
ICondition *FileCache::cond=NULL;

void FileCache::operator()(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();

	while(true)
	{
		std::map<FileCache::SCacheKey, FileCache::SCacheValue> local_buf;

		{
			IScopedLock lock(mutex);

			unsigned int starttime=Server->getTimeMS();
			while(cache_buffer.size()<min_size_no_wait
				&& Server->getTimeMS()-starttime<max_wait_time)
			{
				cond->wait(&lock);
			}

			local_buf=cache_buffer;
			cache_buffer.clear();
		}

		start_transaction();

		for(std::map<FileCache::SCacheKey, FileCache::SCacheValue>::iterator it=local_buf.begin();
			it!=local_buf.end();++it)
		{
			if(it->second.exists)
			{
				put(it->first, it->second);
			}
			else
			{
				del(it->first);
			}
		}

		commit_transaction();
	}
}

void FileCache::put_delayed(const SCacheKey& key, const SCacheValue& value)
{
	IScopedLock lock(mutex);

	while(cache_buffer.size()>=max_buffer_size)
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex);
	}

	cache_buffer[key]=value;
	cond->notify_all();
}

void FileCache::del_delayed(const SCacheKey& key)
{
	put_delayed(key, SCacheValue());
}

FileCache::SCacheValue FileCache::get_with_cache(const FileCache::SCacheKey& key)
{
	{
		IScopedLock lock(mutex);

		std::map<FileCache::SCacheKey, FileCache::SCacheValue>::iterator it=cache_buffer.find(key);

		if(it!=cache_buffer.end())
		{
			return it->second;
		}
	}

	return get(key);
}