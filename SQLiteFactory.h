#include "Interface/DatabaseFactory.h"

class SQLiteFactory : public IDatabaseFactory
{
public:
	IDatabaseInt *createDatabase(void);
};