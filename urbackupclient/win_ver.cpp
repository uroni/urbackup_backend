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

#include "win_ver.h"
#include <stdio.h>
#include <map>
#include "../stringtools.h"

namespace
{
	std::map<std::string, std::string> read_kv(const std::string& cmd)
	{
		FILE* f=_popen(cmd.c_str(), "rt");
		std::map<std::string, std::string> ret;
		if(f!=NULL)
		{
			std::string data;
			char buf[1024];
			while(!feof(f))
			{
				if(fgets(buf, 1024, f))
				{
					data+=buf;
				}				
			}
			_pclose(f);

			int lc = linecount(data);
			for(int i = 0; i < lc; ++i)
			{
				std::string line = getline(i+1, data);

				std::string key = strlower(trim(getuntil("=", line)));
				std::string value = trim(getafter("=", line));

				ret[key]=value;
			}
		}
		return ret;
	}
}


std::string get_windows_version( void )
{
	std::map<std::string, std::string> m = read_kv("wmic os get buildnumber,caption,CSDVersion /value");
	std::string buildnumber = m["buildnumber"];
	std::string caption = m["caption"];
	std::string CSDVersion = m["csdversion"];
	m = read_kv("wmic os get osarchitecture /value");
	std::string osarchitecture = m["osarchitecture"];

	return caption + " "+ CSDVersion +" (build " + buildnumber+")"+ (osarchitecture.empty()?"":(", "+osarchitecture) );
}
