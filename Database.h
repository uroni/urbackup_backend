#include <string>
#include <vector>
#include <map>
#include "Interface/DatabaseInt.h"
#include "Interface/Types.h"
#include "Interface/Mutex.h"
#include "Interface/Condition.h"

struct sqlite3;
class CQuery;

class CDatabase : public IDatabaseInt
{
public:
	bool Open(std::string pFile);
	~CDatabase();
	virtual db_nresults ReadN(std::string pQuery); 
	virtual db_results Read(std::string pQuery); 
	virtual bool Write(std::string pQuery);

	virtual void BeginTransaction(void);
	virtual bool EndTransaction(void);

	virtual IQuery* Prepare(std::string pQuery, bool autodestroy=true);
	virtual IQuery* Prepare(int id, std::string pQuery);
	virtual void destroyQuery(IQuery *q);
	virtual void destroyAllQueries(void);

	virtual _i64 getLastInsertID(void);

	sqlite3 *getDatabase(void);

	//private function
	void InsertResults(const db_nsingle_result &pResult);
	bool WaitForUnlock(void);

	bool LockForTransaction(void);
	void UnlockForTransaction(void);
	bool isInTransaction(void);
	
	static void initMutex(void);
	static void destroyMutex(void);

	virtual bool Import(const std::string &pFile);
	virtual bool Dump(const std::string &pFile);
	
private:

	db_nresults results;
	sqlite3 *db;
	bool in_transaction;

	std::vector<CQuery*> queries;
	std::map<int, IQuery*> prepared_queries;

	static IMutex* lock_mutex;
	static int lock_count;
	static ICondition *unlock_cond;
};

