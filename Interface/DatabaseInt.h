#ifndef IDATABASEINT_H
#define IDATABASEINT_H

#include "Database.h"

class IDatabaseInt : public IDatabase
{
public:
	virtual bool Open(std::string pFile)=0;
};

#endif