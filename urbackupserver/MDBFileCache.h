#include "../Interface/Database.h"
#include "../Interface/Types.h"
#include "lmdb/lmdb.h"

class MDBFileCache
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

		char hash[64];
		int64 filesize;
	};

	static MDBFileCache* getInstance(void);
	static void initFileCache(void);

	MDBFileCache(void);
	~MDBFileCache(void);

	bool has_error(void);

	void create(get_data_callback_t get_data_callback, void *userdata);

	SCacheValue get(const SCacheKey& key);

	void start_transaction(void);

	void put(const SCacheKey& key, const SCacheValue& value);

	void commit_transaction(void);
private:

	void begin_txn(unsigned int flags);

	static MDB_env *env;

	static MDBFileCache *filecache;

	MDB_txn *txn;
	MDB_dbi dbi;
	bool _has_error;
};