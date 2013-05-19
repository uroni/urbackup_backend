#include "DatabaseCursor.h"
#include "Query.h"
#include "sqlite/sqlite3.h"

DatabaseCursor::DatabaseCursor(CQuery *query, int *timeoutms)
	: query(query), transaction_lock(false), tries(60), timeoutms(timeoutms), lastErr(0)
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

	return false;
}