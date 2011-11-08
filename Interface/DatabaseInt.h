#ifndef IDATABASEINT_H
#define IDATABASEINT_H

#include "Database.h"

class IDatabaseInt : public IDatabase
{
public:
	virtual bool Open(std::string pFile, const std::vector<std::pair<std::string,std::string> > &attach)=0;
};

#endif