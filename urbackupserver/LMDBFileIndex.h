#include "../Interface/Database.h"
#include "../Interface/Types.h"
#include "lmdb/lmdb.h"
#include "FileIndex.h"
#include "../Interface/SharedMutex.h"
#include <memory>

class LMDBFileIndex : public FileIndex
{
public:
	static void initFileIndex();
	static void shutdownFileIndex();

	LMDBFileIndex();

	bool create_env( );

	void destroy_env();

	~LMDBFileIndex(void);

	virtual bool has_error(void);

	virtual void create(get_data_callback_t get_data_callback, void *userdata);

	virtual int64 get(const SIndexKey& key);

	virtual int64 get_any_client(const SIndexKey& key);

	virtual int64 get_prefer_client(const SIndexKey& key);

	virtual std::map<int, int64> get_all_clients(const SIndexKey& key);

	virtual void start_transaction(void);

	virtual void put(const SIndexKey& key, int64 value);

	virtual void put(const SIndexKey& key, int64 value, int flags);

	virtual void del(const SIndexKey& key);

	virtual void commit_transaction(void);

	virtual void start_iteration();

	virtual std::map<int, int64> get_next_entries_iteration(bool& has_next);

	virtual void stop_iteration();

	void abort_transaction();

	size_t get_map_size();
private:

	void begin_txn(unsigned int flags);

	static MDB_env *env;
	size_t map_size;

	std::auto_ptr<IScopedReadLock> read_transaction_lock;

	void put_internal(const SIndexKey& key, int64 value, int flags, bool log, bool handle_enosp);

	void del_internal(const SIndexKey& key, bool log, bool handle_enosp);

	void replay_transaction_log();
	
	void commit_transaction_internal(bool handle_enosp);


	MDB_txn *txn;
	MDB_dbi dbi;
	bool _has_error;
	MDB_cursor* it_cursor;

	struct STransactionLogItem
	{
		SIndexKey key;
		int64 value;
		int flags;
	};

	std::vector<STransactionLogItem> transaction_log;

	static ISharedMutex* mutex;
	static LMDBFileIndex* fileindex;
	static THREADPOOL_TICKET fileindex_ticket;
};