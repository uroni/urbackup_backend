/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include <iostream>
#include <memory.h>

#ifndef STATIC_PLUGIN
#define DEF_SERVER
#endif
#include "../Interface/Server.h"

#ifndef STATIC_PLUGIN
IServer *Server;
#else
#include "../StaticPluginRegistration.h"

extern IServer* Server;

#define LoadActions LoadActions_fuseplugin
#define UnloadActions UnloadActions_fuseplugin
#endif

#include "../Interface/Types.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../stringtools.h"

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>

namespace
{
	IVHDFile* vhdfile = NULL;
	__int64 global_offset = 0;

	static const char* volume_path = "/volume";

	static int vhdfile_getattr(const char* path, struct stat* stbuf)
	{
		int res = 0;
		memset(stbuf, 0, sizeof(struct stat));
		if(strcmp(path, "/") == 0)
		{
			stbuf->st_mode = S_IFDIR | 0755;
			stbuf->st_nlink = 2;
		}
		else if(strcmp(path, volume_path) == 0)
		{
			stbuf->st_mode = S_IFREG | 0444;
			stbuf->st_nlink = 1;
			stbuf->st_size = vhdfile->getSize()-global_offset;
		}
		else
		{
			res = -ENOENT;
		}

		return res;
	}

	static int vhdfile_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
							off_t offset, struct fuse_file_info* fi)
	{
		if(strcmp(path, "/") != 0)
			return -ENOENT;

		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		filler(buf, volume_path + 1, NULL, 0);

		return 0;
	}

	static int vhdfile_open(const char* path, struct fuse_file_info* fi)
	{
		if(strcmp(path, volume_path) != 0)
			return -ENOENT;

		if((fi->flags & 3) != O_RDONLY)
			return -EACCES;

		return 0;
	}

	static int vhdfile_read(const char* path, char* buf, size_t size, off_t offset,
							struct fuse_file_info* fi)
	{
		size_t len;
		if(strcmp(path, volume_path) != 0)
			return -ENOENT;

		if(!vhdfile->Seek(offset+global_offset))
		{
			return -EINVAL;
		}
		
		size_t read;
		if(!vhdfile->Read(buf, size, read))
		{
			return -EINVAL;
		}
		
		return static_cast<int>(read);
	}

	static struct fuse_operations vhdfile_oper = {};  
}

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;
	
	std::string vhd_filename = Server->getServerParameter("mount");
	
	if(vhd_filename.empty())
	{
		return;
	}
	else
	{
		Server->Log("Mounting VHD via fuse...", LL_INFO);
	}
	
	std::string mountpoint = Server->getServerParameter("mountpoint");
	
	if(mountpoint.empty())
	{
		Server->Log("Please specify the mountpoint using the --mountpoint parameter", LL_ERROR);
		exit(1);
	}
	
	str_map params;
	IFSImageFactory* image_fak=(IFSImageFactory *)Server->getPlugin(Server->getThreadID(), Server->StartPlugin("fsimageplugin", params));
	if( image_fak==NULL )
	{
		Server->Log("Error loading fsimageplugin", LL_ERROR);
		exit(2);
	}
	
	vhdfile = image_fak->createVHDFile(vhd_filename, true, 0);
	
	if(vhdfile==NULL || !vhdfile->isOpen())
	{
		Server->Log("Error opening VHD file", LL_ERROR);
		exit(3);
	}
	
	std::string offset_s=Server->getServerParameter("offset");
	global_offset=1024*512;
	if(!offset_s.empty())
	{
		global_offset=atoi(offset_s.c_str());
	}
	
	Server->Log("Volume offset is "+convert(global_offset)+" bytes. Configure via --offset", LL_DEBUG);
	
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	
	fuse_chan * ch = fuse_mount(mountpoint.c_str(), &args);
	
	if(!ch)
	{
		Server->Log("Error mounting fuse filesystem", LL_ERROR);
		exit(4);
	}
	
	vhdfile_oper.getattr = vhdfile_getattr;
	vhdfile_oper.readdir = vhdfile_readdir;
	vhdfile_oper.open = vhdfile_open;
	vhdfile_oper.read = vhdfile_read;
	
	fuse* ffuse = fuse_new(ch, &args, &vhdfile_oper, sizeof(vhdfile_oper), NULL);
	
	fuse_opt_free_args(&args);
	
	if(!ffuse)
	{
		Server->Log("Could not initialize fuse", LL_ERROR);
		fuse_unmount(mountpoint.c_str(), ch);
		exit(5);
	}
	
	fuse_set_signal_handlers(fuse_get_session(ffuse));
	
	int rc = fuse_loop(ffuse);
	
	fuse_unmount(mountpoint.c_str(), ch);
	
	if(rc!=0)
	{
		Server->Log("Error occurred while processing fuse events", LL_ERROR);	
		exit(6);
	}
	
	
	fuse_destroy(ffuse);
	exit(0);
}

DLLEXPORT void UnloadActions(void)
{
	Server->Log("Unload");
}

#ifdef STATIC_PLUGIN
namespace
{
	static RegisterPluginHelper register_plugin(LoadActions, UnloadActions, 2);
}
#endif