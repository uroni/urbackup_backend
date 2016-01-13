#include <string>
#include <vector>
#include <map>
#include <memory>
#include "Interface/DatabaseInt.h"
#include "Interface/Types.h"
#include "Interface/Mutex.h"
#include "Interface/Condition.h"
#include "Interface/SharedMutex.h"

struct sqlite3;
class CQuery;

const int c_sqlite_busy_timeout_default=10000; //10 seconds

class CDatabase : public IDatabaseInt
{
public:
	bool Open(std::string pFile, const std::vector<std::pair<std::string,std::string> > &attach,
		size_t allocation_chunk_size);
	~CDatabase();

	virtual db_results Read(std::string pQuery); 
	virtual bool Write(std::string pQuery);

	virtual bool BeginReadTransaction();
	virtual bool BeginWriteTransaction();
	virtual bool EndTransaction(void);
	virtual bool RollbackTransaction();

	virtual IQuery* Prepare(std::string pQuery, bool autodestroy=true);
	virtual IQuery* Prepare(int id, std::string pQuery);
	virtual void destroyQuery(IQuery *q);
	virtual void destroyAllQueries(void);

	virtual _i64 getLastInsertID(void);

	sqlite3 *getDatabase(void);

	//private function
	bool WaitForUnlock(void);

	bool LockForTransaction(void);
	void UnlockForTransaction(void);
	bool isInTransaction(void);
	
	static void initMutex(void);
	static void destroyMutex(void);

	virtual bool Import(const std::string &pFile);
	virtual bool Dump(const std::string &pFile);
	
	virtual std::string getEngineName(void);

	virtual void DetachDBs(void);
	virtual void AttachDBs(void);

	virtual bool Backup(const std::string &pFile);

	virtual void freeMemory();

	virtual int getLastChanges();

	virtual std::string getTempDirectoryPath();

	virtual void lockForSingleUse();

	virtual void unlockForSingleUse();

	ISharedMutex* getSingleUseMutex();
private:
	
	bool backup_db(const std::string &pFile, const std::string &pDB);

	sqlite3 *db;
	bool in_transaction;

	std::vector<CQuery*> queries;
	std::map<int, IQuery*> prepared_queries;

	static IMutex* lock_mutex;
	static int lock_count;
	static ICondition *unlock_cond;
	static ISharedMutex* single_user_mutex;

	std::auto_ptr<IScopedReadLock> transaction_read_lock;
	std::auto_ptr<IScopedWriteLock> write_lock;

	std::vector<std::pair<std::string,std::string> > attached_dbs;
};

