#pragma once

#include "../Interface/Database.h"
#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Thread.h"
#include <memory.h>

class FileCache : public IThread
{
public:
	typedef db_results(*get_data_callback_t)(void *userdata);

	struct SCacheValue
	{
		SCacheValue(std::string fullpath, std::string hashpath)
			: exists(true), fullpath(fullpath), hashpath(hashpath)
		{
		}

		SCacheValue(void)
			: exists(false)
		{
		}

		bool exists;
		std::string fullpath;
		std::string hashpath;
	};

	struct SCacheKey
	{
		SCacheKey(const char thash[64], int64 filesize)
			: filesize(filesize)
		{
			memcpy(hash, thash, 64);
		}

		SCacheKey(void)
			: filesize(-1)
		{
			memset(hash, 0, 64);
		}

		void operator=(const SCacheKey& other)
		{
			memcpy(hash, other.hash, 64);
			filesize=other.filesize;
		}

		bool operator==(const SCacheKey& other) const
		{
			return memcmp(hash, other.hash, 64)==0 && filesize==other.filesize;
		}

		bool operator!=(const SCacheKey& other) const
		{
			return !(*this==other);
		}

		bool operator<(const SCacheKey& other) const
		{
			int mres=memcmp(hash, other.hash, 64);
			return mres<0 ||
				(mres==0 && filesize<other.filesize);
		}

		char hash[64];
		int64 filesize;
	};

	virtual ~FileCache(void) {};

	virtual bool has_error(void)=0;

	virtual void create(get_data_callback_t get_data_callback, void *userdata)=0;

	virtual SCacheValue get(const SCacheKey& key)=0;

	virtual void start_transaction(void)=0;

	virtual void put(const SCacheKey& key, const SCacheValue& value)=0;

	static void put_delayed(const SCacheKey& key, const SCacheValue& value);

	virtual SCacheValue get_with_cache(const SCacheKey& key);

	virtual void del(const SCacheKey& key)=0;

	static void del_delayed(const SCacheKey& key);

	virtual void commit_transaction(void)=0;

	void operator()(void);

private:

	static std::map<SCacheKey, SCacheValue> cache_buffer;
	static std::map<SCacheKey, bool> del_buffer;
	static IMutex *mutex;
	static ICondition *cond;
};