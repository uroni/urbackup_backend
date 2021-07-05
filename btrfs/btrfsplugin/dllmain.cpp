/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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

#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#include <iostream>
#include <memory.h>
#include <stdio.h>
#include <string>
#include <assert.h>

#ifndef STATIC_PLUGIN
#define DEF_SERVER
#endif

#include "../../Interface/Server.h"

#ifndef STATIC_PLUGIN
IServer* Server;
#else
#include "../../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_btrfsplugin
#define UnloadActions UnloadActions_btrfsplugin
#endif

#include <stdlib.h>

#include "pluginmgr.h"
#include "../fuse/fuse.h"
#include "IBtrfsFactory.h"

BtrfsPluginMgr* btrfspluginmgr;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server = pServer;

	btrfs_fuse_init();

	btrfspluginmgr = new BtrfsPluginMgr;

	if (!Server->getServerParameter("run_btrfs_test").empty())
	{
		str_map params;
		IBtrfsFactory* fak = reinterpret_cast<IBtrfsFactory*>(btrfspluginmgr->createPluginInstance(params));
		IBackupFileSystem* fs = fak->openBtrfsImage(Server->openFile("D:\\tmp\\btrfs.img", MODE_RW));
		bool b = fs->deleteFile("test_dir2");
		assert(b);
		b = fs->createDir("test_dir2");
		assert(b);
		IFsFile* f = fs->openFile("test_file_5", MODE_WRITE);
		std::string buf;
		buf.resize(4096);
		f->Write(buf);
		f->Seek(0);
		f->Read(4096);
		f->Resize(4096);
		assert(f->Size() == 4096);
		b = fs->sync(std::string());
		assert(b);
		exit(0);
	}

	Server->RegisterPluginThreadsafeModel(btrfspluginmgr, "btrfsplugin");

#ifndef STATIC_PLUGIN
	Server->Log("Loaded -btrfsplugin- plugin", LL_INFO);
#endif
}

DLLEXPORT void UnloadActions(void)
{

}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 0);
}
#endif
