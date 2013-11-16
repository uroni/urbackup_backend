#ifndef DATABASECURSOR_H_
#define DATABASECURSOR_H_

#include "Interface/DatabaseCursor.h"
#include "Interface/Object.h"
#include "Query.h"

class CQuery;

class DatabaseCursor : public IDatabaseCursor
{
public:
	DatabaseCursor(CQuery *query, int *timeoutms);
	~DatabaseCursor(void);

	bool next(db_single_result &res);

	bool has_error(void);

private:
	CQuery *query;

	bool transaction_lock;
	int tries;
	int *timeoutms;
	int lastErr;
	bool _has_error;

#ifdef DEBUG_QUERIES
	ScopedAddActiveQuery *active_query;
#endif
};

#endif //DATABASECURSOR_H_