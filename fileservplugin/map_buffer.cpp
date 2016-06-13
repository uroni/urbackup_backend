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

#include <string>
#include <map>

#include "../vld.h"
#include "CriticalSection.h"
#include "types.h"
#include "../stringtools.h"
#include "log.h"
#include "settings.h"
#include "../Interface/Server.h"
#include "map_buffer.h"

struct s_mapl
{
	std::string value;
	bool allow_exec;
};

namespace
{
	std::map<std::pair<std::string, std::string>, s_mapl> mapbuffer;
	CriticalSection mapcs;
	std::map<std::string, std::string> fn_redirects;
}


std::string getOsDir(std::string input)
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

std::string map_file(std::string fn, const std::string& identity, bool& allow_exec)
{
	allow_exec = false;

	std::string ts=getuntil("/",fn);
	if(ts.empty())
		ts=fn;
	fn.erase(0,ts.size());

	std::string cp;
	for(size_t i=0;i<fn.size();++i)
	{
		if(fn[i]=='/')
		{
			if(cp=="." || cp=="..")
			{
				return "";
			}
			cp.clear();
		}
		else
		{
			cp+=fn[i];
		}
	}

	mapcs.Enter();
	std::map<std::pair<std::string, std::string>, s_mapl>::iterator i=mapbuffer.find(std::make_pair(ts, std::string()));

	if(i==mapbuffer.end())
	{
		i=mapbuffer.find(std::make_pair(ts, identity));
	}

	if(i==mapbuffer.end() )
	{
		mapcs.Leave();
		Log("Could not find share \""+ts+"\"", LL_DEBUG);
		return "";
	}
	else
	{
		std::string ret;
		if (i->second.value != "/" || fn.empty() || fn[0] != '/')
		{
			ret = i->second.value + getOsDir(fn);
		}
		else
		{
			ret = getOsDir(fn);
		}

#ifndef _WIN32
		if (ret.empty())
		{
			ret = "/";
		}
#endif

		std::map<std::string, std::string>::iterator redir_it = fn_redirects.find(ret);
		if (redir_it != fn_redirects.end())
		{
			ret = redir_it->second;
		}

		allow_exec = i->second.allow_exec;
	    	
		mapcs.Leave();
        return ret;
	}
}

void add_share_path(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec)
{
	s_mapl m;
	m.value=getOsDir(path);
	m.allow_exec = allow_exec;
	
	mapcs.Enter();
	mapbuffer[std::make_pair(name, identity)]=m;
	mapcs.Leave();
}

bool remove_share_path(const std::string &name, const std::string& identity)
{
	mapcs.Enter();

	std::map<std::pair<std::string, std::string>, s_mapl>::iterator it=mapbuffer.find(std::make_pair(name, identity));
	if(it!=mapbuffer.end())
	{
		mapbuffer.erase(it);
		mapcs.Leave();
		return true;
	}

	mapcs.Leave();
	return false;
}

void register_fn_redirect(const std::string & source_fn, const std::string & target_fn)
{
	mapcs.Enter();

	fn_redirects[source_fn] = target_fn;

	mapcs.Leave();
}
