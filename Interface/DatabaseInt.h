#ifndef IDATABASEINT_H
#define IDATABASEINT_H

#include "Database.h"

class ISharedMutex;
class IMutex;
class ICondition;

class IDatabaseInt : public IDatabase
{
public:
	virtual bool Open(std::string pFile, const std::vector<std::pair<std::string, std::string> > &attach,
		size_t allocation_chunk_size, ISharedMutex* single_user_mutex, IMutex* lock_mutex,
		int* lock_count, ICondition *unlock_cond, const str_map& params) = 0;
};

#endif