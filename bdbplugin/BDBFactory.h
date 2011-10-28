#include "../Interface/DatabaseFactory.h"
#include "../Interface/DatabaseInt.h"

class BDBFactory : public IDatabaseFactory
{
public:
	virtual IDatabaseInt *createDatabase(void);
};