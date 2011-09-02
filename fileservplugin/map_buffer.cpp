/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
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

struct s_mapl
{
	std::wstring value;
	_u32 lastmaptime;
};

std::map<std::wstring, s_mapl> mapbuffer;
CriticalSection mapcs;

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


std::wstring getFileName(const std::wstring &fn, const std::wstring &value, bool append_urd, std::wstring *udir=NULL)
{
	std::wstring urd=L"urinstaller";
	std::wstring dir=value;

	/*if( value.find(L"|")!=std::string::npos )
	{
		urd=getafter(L"|",value);
		urd.erase(0,1);
		dir=getuntil(L"|",value);

		if( udir!=NULL )
			*udir=urd;
	}*/

	if( append_urd==true )
#ifdef _WIN32
		return dir+L"\\"+urd+fn;
#else
		return dir+L"/"+urd+fn;
#endif
	else
#ifdef _WIN32
		return dir+fn;
#else
		return dir+fn;
#endif
}

std::wstring map_file(std::wstring fn, bool append_urd, std::wstring *udir=NULL)
{
	if(fn==L"speedtest")
		return testfilename;

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
	std::map<std::wstring, s_mapl>::iterator i=mapbuffer.find(ts);

	if(i==mapbuffer.end() )
	{
		mapcs.Leave();
		return L"";
	}
	else
	{
		mapcs.Leave();
		return getFileName(getOsDir(fn), i->second.value,append_urd, udir);
	}
}

void add_share_path(const std::wstring &name, const std::wstring &path)
{
	s_mapl m;
	m.value=getOsDir(path);
	
	mapcs.Enter();
	mapbuffer[name]=m;
	mapcs.Leave();
}

void remove_share_path(const std::wstring &name)
{
	mapcs.Enter();

	std::map<std::wstring, s_mapl>::iterator it=mapbuffer.find(name);
	if(it!=mapbuffer.end())
	{
		mapbuffer.erase(it);
	}

	mapcs.Leave();
}

std::vector<std::wstring> get_maps(void)
{
	std::vector<std::wstring> ret;
	mapcs.Enter();
	for(std::map<std::wstring, s_mapl>::iterator it=mapbuffer.begin();it!=mapbuffer.end();++it)
	{
		ret.push_back(it->first);
	}
	mapcs.Leave();
	return ret;
}
