#include "../Interface/Database.h"
#include "../Interface/Types.h"
#include "lmdb/lmdb.h"
#include "FileCache.h"

class MDBFileCache : public FileCache
{
public:
	static void initFileCache(size_t map_size);

	MDBFileCache(size_t map_size);
	~MDBFileCache(void);

	virtual bool has_error(void);

	virtual void create(get_data_callback_t get_data_callback, void *userdata);

	virtual SCacheValue get(const SCacheKey& key);

	virtual void start_transaction(void);

	virtual void put(const SCacheKey& key, const SCacheValue& value);

	virtual void del(const SCacheKey& key);

	virtual void commit_transaction(void);
private:

	void begin_txn(unsigned int flags);

	static MDB_env *env;

	MDB_txn *txn;
	MDB_dbi dbi;
	bool _has_error;
};