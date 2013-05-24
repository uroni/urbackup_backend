#include "../Interface/Database.h"
#include "../Interface/Types.h"
#include <memory.h>

class FileCache
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

		char hash[64];
		int64 filesize;
	};

	virtual ~FileCache(void) {};

	virtual bool has_error(void)=0;

	virtual void create(get_data_callback_t get_data_callback, void *userdata)=0;

	virtual SCacheValue get(const SCacheKey& key)=0;

	virtual void start_transaction(void)=0;

	virtual void put(const SCacheKey& key, const SCacheValue& value)=0;

	virtual void del(const SCacheKey& key)=0;

	virtual void commit_transaction(void)=0;
};