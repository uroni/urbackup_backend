#ifndef IDATABASECURSOR_H_
#define IDATABASECURSOR_H_

#include "Query.h"

class IDatabaseCursor
{
public:
	virtual bool next(db_single_result &res)=0;

	virtual bool has_error()=0;

	virtual void shutdown() = 0;
};

class ScopedDatabaseCursor
{
public:
	ScopedDatabaseCursor(IDatabaseCursor* cursor)
		: cursor(cursor)
	{

	}

	~ScopedDatabaseCursor()
	{
		cursor->shutdown();
	}

	virtual bool next(db_single_result &res)
	{
		return cursor->next(res);
	}

	virtual bool has_error()
	{
		return cursor->has_error();
	}

private:
	IDatabaseCursor* cursor;
};

#endif //IDATABASECURSOR_H_