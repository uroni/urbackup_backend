#include "../Interface/Database.h"
#include "../Interface/Types.h"
#include "FileCache.h"

class SQLiteFileCache : public FileCache
{
public:
	static void initFileCache(void);

	SQLiteFileCache(void);
	~SQLiteFileCache(void);

	virtual bool has_error(void);

	virtual void create(get_data_callback_t get_data_callback, void *userdata);

	virtual SCacheValue get(const SCacheKey& key);

	virtual void start_transaction(void);

	virtual void put(const SCacheKey& key, const SCacheValue& value);

	virtual void del(const SCacheKey& key);

	virtual void commit_transaction(void);
private:

	void setup_queries(void);

	IDatabase *db;

	IQuery *q_put;
	IQuery *q_del;
	IQuery *q_get;

	bool _has_error;
};