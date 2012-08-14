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
#include <dlfcn.h>

bool CServer::LoadDLL(const std::string &name)
{
	int mode=RTLD_NOW | RTLD_GLOBAL;
	if(getServerParameter("leak_check")=="true")
	{
		mode|=RTLD_NODELETE;
	}
	HMODULE dll = dlopen( name.c_str(), mode);

	if(dll==NULL)
	{
		Server->Log("DLL not found: "+(std::string)dlerror(), LL_ERROR);
		return false;
	}

	unload_handles.push_back( dll );

	LOADACTIONS load_func=NULL;
	load_func=(LOADACTIONS)dlsym(dll,"LoadActions");
	unload_functs.insert(std::pair<std::string, UNLOADACTIONS>(name, (UNLOADACTIONS) dlsym(dll,"UnloadActions") ) );

	if(load_func==NULL)
	{
		Server->Log("Loading function in DLL not found", LL_ERROR);
		return false;
	}

	load_func(this);
	return true;
}

void CServer::UnloadDLLs2(void)
{
	for(size_t i=0;i<unload_handles.size();++i)
	{
		dlclose( unload_handles[i] );
	}
}

int CServer::WriteDump(void* pExceptionPointers)
{
}
