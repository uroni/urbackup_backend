#ifndef DATABASECURSOR_H_
#define DATABASECURSOR_H_

#include "Interface/DatabaseCursor.h"
#include "Interface/Object.h"

class CQuery;

class DatabaseCursor : public IDatabaseCursor
{
public:
	DatabaseCursor(CQuery *query, int *timeoutms);
	~DatabaseCursor(void);

	bool next(db_single_result &res);

private:
	CQuery *query;

	bool transaction_lock;
	int tries;
	int *timeoutms;
	int lastErr;
};

#endif //DATABASECURSOR_H_