#pragma once
#include "../../Interface/Database.h"

class ServerCleanupDAO
{
public:

	ServerCleanupDAO(IDatabase *db);
	~ServerCleanupDAO(void);

	//@-SQLGenFunctionsBegin
	//@-SQLGenFunctionsEnd

private:
	void createQueries(void);
	void destroyQueries(void);

	IDatabase *db;

	//@-SQLGenVariablesBegin
	//@-SQLGenVariablesEnd
};