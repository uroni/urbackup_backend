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

#include "vld.h"
#include "Server.h"
#include "stringtools.h"
#include <windows.h>

bool CServer::LoadDLL(const std::string &name)
{
	HMODULE dll = LoadLibraryA( name.c_str() );

	if(dll==NULL)
	{
		Server->Log("Loading DLL \""+name+"\" failed. Error Code: "+nconvert((int)GetLastError()));
		return false;
	}

	unload_handles.push_back( dll );

	LOADACTIONS load_func=NULL;
	load_func=(LOADACTIONS)GetProcAddress(dll,"LoadActions");
	unload_functs.insert(std::pair<std::string, UNLOADACTIONS>(name, (UNLOADACTIONS) GetProcAddress(dll,"UnloadActions") ) );

	if(load_func==NULL)
		return false;

	load_func(this);
	return true;
}

void CServer::UnloadDLLs2(void)
{
	for(size_t i=0;i<unload_handles.size();++i)
	{
		FreeLibrary( unload_handles[i] );
	}
}
