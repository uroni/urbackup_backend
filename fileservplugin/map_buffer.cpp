/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include <string>
#include <map>

#include "../vld.h"
#include "CriticalSection.h"
#include "types.h"
#include "../stringtools.h"
#include "log.h"
#include "settings.h"
#include "../Interface/Server.h"

struct s_mapl
{
	std::wstring value;
	_u32 lastmaptime;
};

namespace
{
	std::map<std::pair<std::wstring, std::string>, s_mapl> mapbuffer;
	CriticalSection mapcs;
}


std::wstring getOsDir(std::wstring input)
{
#ifdef _WIN32
	for(size_t i=0;i<input.size();++i)
	{
		if(input[i]=='/')
			input[i]='\\';
	}
#endif
	return input;
}

std::wstring map_file(std::wstring fn, const std::string& identity)
{
	std::wstring ts=getuntil(L"/",fn);
	if(ts.empty())
		ts=fn;
	fn.erase(0,ts.size());

	std::wstring cp;
	for(size_t i=0;i<fn.size();++i)
	{
		if(fn[i]=='/')
		{
			if(cp==L"." || cp==L"..")
			{
				return L"";
			}
			cp.clear();
		}
		else
		{
			cp+=fn[i];
		}
	}

	mapcs.Enter();
	std::map<std::pair<std::wstring, std::string>, s_mapl>::iterator i=mapbuffer.find(std::make_pair(ts, std::string()));

	if(i==mapbuffer.end())
	{
		i=mapbuffer.find(std::make_pair(ts, identity));
	}

	if(i==mapbuffer.end() )
	{
		mapcs.Leave();
		Log("Could not find share \""+Server->ConvertToUTF8(ts)+"\"", LL_WARNING);
		return L"";
	}
	else
	{
		std::wstring ret;
		if(i->second.value!=L"/" || fn.empty() || fn[0]!='/')
	            ret = i->second.value + getOsDir(fn);
	        else
	    	    ret = getOsDir(fn);
	    	
		mapcs.Leave();
        return ret;
	}
}

void add_share_path(const std::wstring &name, const std::wstring &path, const std::string& identity)
{
	s_mapl m;
	m.value=getOsDir(path);
	
	mapcs.Enter();
	mapbuffer[std::make_pair(name, identity)]=m;
	mapcs.Leave();
}

void remove_share_path(const std::wstring &name, const std::string& identity)
{
	mapcs.Enter();

	std::map<std::pair<std::wstring, std::string>, s_mapl>::iterator it=mapbuffer.find(std::make_pair(name, identity));
	if(it!=mapbuffer.end())
	{
		mapbuffer.erase(it);
	}

	mapcs.Leave();
}
