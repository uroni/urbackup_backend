#include "Interface/Query.h"

struct sqlite3_stmt;
struct sqlite3;

class CDatabase;

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

	virtual bool Write(void);
	db_results Read(void);
	db_nresults ReadN(void);

private:
	bool Execute(void);

	sqlite3_stmt *ps;
	std::string stmt_str;
	CDatabase *db;
	int curr_idx;
};
