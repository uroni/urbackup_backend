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

#include "win_disk_mon.h"
#include <stdio.h>
#include <map>
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"

namespace
{
	std::vector<std::map<std::string, std::string> > read_kvs(const std::string& cmd)
	{
		std::string output;
		int rc = os_popen(cmd.c_str(), output);

		std::vector<std::map<std::string, std::string> > ret;
		if (rc == 0)
		{
			std::map<std::string, std::string> curr;
			int lc = linecount(output);
			for (int i = 0; i < lc; ++i)
			{
				std::string line = trim(getline(i + 1, output));

				if (line.empty())
				{
					if (!curr.empty())
					{
						ret.push_back(curr);
						curr.clear();
					}
				}
				else
				{
					std::string key = strlower(trim(getuntil("=", line)));
					std::string value = trim(getafter("=", line));

					curr[key] = value;
				}
			}

			if (!curr.empty())
			{
				ret.push_back(curr);
			}
		}
		return ret;
	}
}


std::vector<SFailedDisk> get_failed_disks()
{
	std::vector<std::map<std::string, std::string> > m = read_kvs("wmic diskdrive get caption,status,statusinfo /value");

	std::vector<SFailedDisk> ret;
	for (size_t i = 0; i < m.size(); ++i)
	{
		if (strlower(m[i]["status"]) != "ok")
		{
			SFailedDisk fd;
			fd.name = m[i]["caption"];
			fd.status = m[i]["status"];
			fd.status_info = m[i]["statusinfo"];
			ret.push_back(fd);
		}
	}

	return ret;
}
