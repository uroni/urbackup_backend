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

#include "../vld.h"
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif


#define DEF_SERVER
#include "../Interface/Server.h"

#ifndef STATIC_PLUGIN
IServer *Server;
#else
#include "../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_fileservplugin
#define UnloadActions UnloadActions_fileservplugin
#endif


#include "pluginmgr.h"
#include "FileServ.h"
#include "IFileServFactory.h"
#include "IFileServ.h"
#include "../stringtools.h"
#include <stdlib.h>

CFileServPluginMgr *fileservpluginmgr=NULL;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	FileServ::init_mutex();

	fileservpluginmgr=new CFileServPluginMgr;

	Server->RegisterPluginThreadsafeModel( fileservpluginmgr, "fileserv");

	std::string share_dir=Server->getServerParameter("fileserv_share_dir");
	if(!share_dir.empty())
	{
		str_map params;
		IFileServFactory *fileserv_fak=(IFileServFactory*)fileservpluginmgr->createPluginInstance(params);
		unsigned short tcpport=43001;
		unsigned short udpport=43002;

		std::string s_tcpport=Server->getServerParameter("fileserv_tcpport");
		if(!s_tcpport.empty())
			tcpport=atoi(s_tcpport.c_str());
		std::string s_udpport=Server->getServerParameter("fileserv_udpport");
		if(!s_udpport.empty())
			udpport=atoi(s_udpport.c_str());

		IFileServ *fileserv=fileserv_fak->createFileServ(tcpport, udpport);
		fileserv->shareDir(widen(ExtractFileName(share_dir)), widen(share_dir), std::string());
		fileserv->addIdentity("");
	}

	Server->Log("Loaded -fileserv- plugin", LL_INFO);
}

DLLEXPORT void UnloadActions(void)
{
	if(Server->getServerParameter("leak_check")=="true")
	{
		FileServ::destroy_mutex();
	}
}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 0);
}
#endif