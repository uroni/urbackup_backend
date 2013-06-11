#include "DatabaseCursor.h"
#include "Query.h"
#include "sqlite/sqlite3.h"
#include "Server.h"

DatabaseCursor::DatabaseCursor(CQuery *query, int *timeoutms)
	: query(query), transaction_lock(false), tries(60), timeoutms(timeoutms), lastErr(0), _has_error(false)
{
	query->setupStepping(timeoutms);
}

DatabaseCursor::~DatabaseCursor(void)
{
	query->shutdownStepping(lastErr, timeoutms, transaction_lock);
}

bool DatabaseCursor::next(db_single_result &res)
{
	res.clear();
	int rc;
	do
	{
		rc=query->step(res, timeoutms, tries, transaction_lock);
		if(rc==SQLITE_ROW)
		{
			return true;
		}
	}
	while(query->resultOkay(rc));

	if(rc!=SQLITE_OK && rc!=SQLITE_DONE)
	{
		Server->Log("SQL Error: "+(std::string)sqlite3_errmsg(db->getDatabase())+ " Stmt: ["+query->getStatement()+"]", LL_ERROR);
		_has_error=true;
	}

	return false;
}

bool DatabaseCursor::has_error(void)
{
	return _has_error;
}