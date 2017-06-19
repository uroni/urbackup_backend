/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "DataplanDb.h"
#include "../Interface/Server.h"
#include <fstream>
#include "../stringtools.h"
#include "../urbackupcommon/glob.h"

DataplanDb* DataplanDb::instance = NULL;

DataplanDb::DataplanDb()
	: mutex(Server->createMutex())
{
}

DataplanDb * DataplanDb::getInstance()
{
	return instance;
}

void DataplanDb::init()
{
	instance = new DataplanDb;
	instance->read("urbackup/dataplan_db.txt");
}

bool DataplanDb::read(const std::string & fn)
{
	std::fstream in(fn.c_str(), std::ios::in | std::ios::binary);

	if (!in.is_open())
	{
		return false;
	}

	IScopedLock lock(mutex.get());

	items.clear();
	cache.clear();

	std::string line;
	while (std::getline(in, line))
	{
		size_t sep = line.find(" ");
		if (sep == std::string::npos
			|| sep+1>=line.size())
			continue;

		SDataplanItem item;
		item.hostname_glob = trim(line.substr(0, sep));
		item.limit = watoi64(line.substr(sep + 1));
		items.push_back(item);
	}

	return true;
}

bool DataplanDb::getLimit(const std::string & hostname, std::string& pattern, int64& limit)
{
	IScopedLock lock(mutex.get());

	SDataplanItem* item = cache.get(hostname);
	if (item != NULL)
	{
		if (item->limit < 0)
		{
			return false;
		}
		limit = item->limit;
		pattern = item->hostname_glob;
		return true;
	}

	for (size_t i = 0; i < items.size(); ++i)
	{
		if (amatch(hostname.c_str(), items[i].hostname_glob.c_str()))
		{
			limit = items[i].limit*1024*1024;
			pattern = items[i].hostname_glob;

			SDataplanItem ldi = items[i];
					
			cache.put(hostname, ldi);

			while (cache.size() > 100)
			{
				cache.evict_one();
			}

			return true;
		}
	}

	SDataplanItem di;
	di.limit = -1;
	cache.put(hostname, di);

	while (cache.size() > 100)
	{
		cache.evict_one();
	}

	return false;
}
