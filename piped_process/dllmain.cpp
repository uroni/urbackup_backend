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

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif


#define DEF_SERVER
#include "../Interface/Server.h"

#include "pluginmgr.h"

IServer *Server;

CPipedProcessPluginMgr *pipedprocesspluginmgr=NULL;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	pipedprocesspluginmgr=new CPipedProcessPluginMgr;

	Server->RegisterPluginThreadsafeModel( pipedprocesspluginmgr, "piped_process");

	Server->Log("Loaded -piped_process- plugin", LL_INFO);
}

DLLEXPORT void UnloadActions(void)
{
}
