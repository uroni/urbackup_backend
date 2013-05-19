#include "Interface/Query.h"

struct sqlite3_stmt;
struct sqlite3;

class CDatabase;
class DatabaseCursor;

class CQuery : public IQuery
{
public:
	CQuery(const std::string &pStmt_str, sqlite3_stmt *prepared_statement, CDatabase *pDB);
	~CQuery();

	virtual void Bind(const std::string &str);
	virtual void Bind(const std::wstring &str);
	virtual void Bind(int p);
	virtual void Bind(unsigned int p);
	virtual void Bind(double p);
	virtual void Bind(int64 p);
#if defined(_WIN64) || defined(_LP64)
	virtual void Bind(size_t p);
#endif
	virtual void Bind(const char* buffer, _u32 bsize);

	virtual void Reset(void);

	virtual bool Write(int timeoutms=-1);
	db_results Read(int *timeoutms=NULL);
	db_nresults ReadN(int *timeoutms=NULL);

	virtual IDatabaseCursor* Cursor(int *timeoutms=NULL);

	void setupStepping(int *timeoutms);
	void shutdownStepping(int err, int *timeoutms, bool& transaction_lock);

	int step(db_single_result& res, int *timeoutms, int& tries, bool& transaction_lock);

	int stepN(db_nsingle_result& res, int *timeoutms, int& tries, bool& transaction_lock);

	bool resultOkay(int rc);

private:
	bool Execute(int timeoutms);

	sqlite3_stmt *ps;
	std::string stmt_str;
	CDatabase *db;
	int curr_idx;
	DatabaseCursor *cursor;
};
