#ifndef IDATABASEINT_H
#define IDATABASEINT_H

#include "Database.h"

class IDatabaseInt : public IDatabase
{
public:
	virtual bool Open(std::string pFile, const std::vector<std::pair<std::string,std::string> > &attach,
		size_t allocation_chunk_size)=0;
};

#endif