#include "FileIndex.h"
#include "../Interface/Server.h"

const size_t max_buffer_size=1000;
#ifdef _DEBUG
const unsigned int max_wait_time=1000;
#else
const unsigned int max_wait_time=120000;
#endif
const size_t min_size_no_wait=100;

std::map<FileIndex::SIndexKey, int64> FileIndex::cache_buffer_1;
std::map<FileIndex::SIndexKey, int64> FileIndex::cache_buffer_2;
std::map<FileIndex::SIndexKey, int64>* FileIndex::active_cache_buffer=&cache_buffer_1;
std::map<FileIndex::SIndexKey, int64>* FileIndex::other_cache_buffer=&cache_buffer_2;



IMutex *FileIndex::mutex=NULL;
ICondition *FileIndex::cond=NULL;
bool FileIndex::do_shutdown=false;

void FileIndex::operator()(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();

	while(true)
	{
		std::map<FileIndex::SIndexKey, int64>* local_buf;

		{
			IScopedLock lock(mutex);

			if(do_shutdown &&
				cache_buffer_1.empty() &&
				cache_buffer_2.empty() )
			{
				break;
			}

			while(active_cache_buffer->empty())
			{
				int64 starttime=Server->getTimeMS();

				while(active_cache_buffer->size()<min_size_no_wait
					&& Server->getTimeMS()-starttime<max_wait_time
					&& !do_shutdown)
				{
					cond->wait(&lock, max_wait_time);
				}
			}			

			local_buf=active_cache_buffer;
			
			if(active_cache_buffer==&cache_buffer_1)
			{
				active_cache_buffer=&cache_buffer_2;
				other_cache_buffer=&cache_buffer_1;
			}
			else
			{
				active_cache_buffer=&cache_buffer_1;
				other_cache_buffer=&cache_buffer_2;
			}
		}

		start_transaction();

		for(std::map<FileIndex::SIndexKey, int64>::iterator it=local_buf->begin();
			it!=local_buf->end();++it)
		{
			if(it->second!=0)
			{
				put(it->first, it->second);
			}
			else
			{
				del(it->first);
			}
		}

		commit_transaction();

		{
			IScopedLock lock(mutex);
			local_buf->clear();
		}
	}

	delete this;
}

void FileIndex::put_delayed(const SIndexKey& key, int64 value)
{
	IScopedLock lock(mutex);

	while(active_cache_buffer->size()>=max_buffer_size)
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex);
	}

	(*active_cache_buffer)[key]=value;
	cond->notify_all();
}

void FileIndex::del_delayed(const SIndexKey& key)
{
	put_delayed(key, 0);
}

int64 FileIndex::get_with_cache(const FileIndex::SIndexKey& key)
{
	{
		IScopedLock lock(mutex);

		int64 ret;
		if(get_from_cache(key, *active_cache_buffer, ret))
		{
			return ret;
		}

		if(get_from_cache(key, *other_cache_buffer, ret))
		{
			return ret;
		}

	}

	return get_any_client(key);
}

int64 FileIndex::get_with_cache_prefer_client(const SIndexKey& key)
{
	{
		IScopedLock lock(mutex);

		int64 ret;
		if(get_from_cache_prefer_client(key, *active_cache_buffer, ret))
		{
			return ret;
		}

		if(get_from_cache_prefer_client(key, *other_cache_buffer, ret))
		{
			return ret;
		}
	}

	return get_prefer_client(key);
}

std::map<int, int64> FileIndex::get_all_clients_with_cache( const SIndexKey& key, bool with_del)
{
	std::map<int, int64> ret = get_all_clients(key);


	{
		IScopedLock lock(mutex);

		get_from_cache_all_clients(key, *other_cache_buffer, ret);

		get_from_cache_all_clients(key, *active_cache_buffer, ret);
	}
	
	if(!with_del)
	{
		for(std::map<int, int64>::iterator it=ret.begin();it!=ret.end();)
		{
			if(it->second == 0)
			{
				std::map<int, int64>::iterator del_it = it;
				++it;
				ret.erase(del_it);
			}
			else
			{
				++it;
			}
		}
	}

	return ret;
}

int64 FileIndex::get_with_cache_exact( const SIndexKey& key )
{
	{
		IScopedLock lock(mutex);

		int64 ret;
		if(get_from_cache_exact(key, *active_cache_buffer, ret))
		{
			return ret;
		}

		if(get_from_cache_exact(key, *other_cache_buffer, ret))
		{
			return ret;
		}
	}

	return get(key);
}

void FileIndex::shutdown()
{
	IScopedLock lock(mutex);

	do_shutdown=true;
	cond->notify_all();
}

bool FileIndex::get_from_cache( const FileIndex::SIndexKey &key, const std::map<SIndexKey, int64>& cache, int64& res )
{
	std::map<FileIndex::SIndexKey, int64>::const_iterator it=cache.lower_bound(key);

	if(it!=cache.end() &&
		it->first.isEqualWithoutClientid(key))
	{
		res = it->second;
		return true;
	}

	return false;
}

bool FileIndex::get_from_cache_prefer_client( const SIndexKey &key, const std::map<SIndexKey, int64>& cache, int64& res)
{
	std::map<FileIndex::SIndexKey, int64>::const_iterator start_it=cache.lower_bound(key);

	if(start_it!=cache.end()
		&& start_it->first.isEqualWithoutClientid(key) )
	{
		res=start_it->second;
		return true;
	}

	if(start_it!=cache.end() &&
		start_it!=cache.begin())
	{
		--start_it;

		if( start_it->first.isEqualWithoutClientid(key) )
		{
			res=start_it->second;
			return true;
		}
	}

	return false;
}

void FileIndex::get_from_cache_all_clients( const SIndexKey &key, const std::map<SIndexKey, int64>& cache, std::map<int, int64> &ret )
{
	std::map<FileIndex::SIndexKey, int64>::const_iterator it=cache.lower_bound(key);

	for(;it!=cache.end()
		&& it->first.isEqualWithoutClientid(key); ++it)
	{
		ret[it->first.getClientid()]=it->second;
	}
}

bool FileIndex::get_from_cache_exact( const SIndexKey& key, const std::map<SIndexKey, int64>& cache, int64& res )
{
	std::map<FileIndex::SIndexKey, int64>::const_iterator it=cache.find(key);

	if(it!=cache.end())
	{
		res=it->second;
		return true;
	}

	return false;
}
