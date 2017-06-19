#pragma once

#include "../Interface/Types.h"
#include "../Interface/Mutex.h"
#include <memory>
#include <map>
#include "../common/lrucache.h"

class DataplanDb
{
public:
	DataplanDb();

	static DataplanDb* getInstance();
	static void init();

	struct SDataplanItem
	{
		std::string hostname_glob;
		int64 limit;
	};

	bool read(const std::string& fn);

	bool getLimit(const std::string& hostname, std::string& pattern, int64& limit);

private:
	static DataplanDb* instance;
	std::auto_ptr<IMutex> mutex;
	std::vector<SDataplanItem> items;
	common::lrucache<std::string, SDataplanItem> cache;
	
};