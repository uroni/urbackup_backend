#ifndef IDATABASECURSOR_H_
#define IDATABASECURSOR_H_

#include "Query.h"

class IDatabaseCursor
{
public:
	virtual bool next(db_single_result &res)=0;

	virtual bool has_error(void)=0;
};

#endif //IDATABASECURSOR_H_