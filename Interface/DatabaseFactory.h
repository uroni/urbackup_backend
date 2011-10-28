#ifndef IDATABASEFACTORY_H
#define IDATABASEFACTORY_H

#include "Object.h"
#include "DatabaseInt.h"

class IDatabaseFactory : public IObject
{
public:
	virtual IDatabaseInt *createDatabase(void)=0;
};

#endif //IDATABASEFACTORY_H