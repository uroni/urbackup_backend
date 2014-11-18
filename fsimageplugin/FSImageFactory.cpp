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

#include "FSImageFactory.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"

#include "fs/ntfs.h"
#ifdef _WIN32
#include "fs/ntfs_win.h"
#define FSNTFS FSNTFSWIN
#endif
#include "fs/unknown.h"
#include "vhdfile.h"
#include "../stringtools.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#include "cowfile.h"
#endif


void PrintInfo(IFilesystem *fs)
{
	Server->Log("FSINFO: blocksize="+nconvert(fs->getBlocksize())+" size="+nconvert(fs->getSize())+" has_error="+nconvert(fs->hasError())+" used_space="+nconvert(fs->calculateUsedSpace()), LL_DEBUG);
}

IFilesystem *FSImageFactory::createFilesystem(const std::wstring &pDev)
{
	IFile *dev=Server->openFile(pDev, MODE_READ_DEVICE);
	if(dev==NULL)
	{
		int last_error;
#ifdef _WIN32
		last_error=GetLastError();
#else
		last_error=errno;
#endif
		Server->Log(L"Error opening device file ("+pDev+L") Errorcode: "+convert(last_error), LL_ERROR);
		return NULL;
	}
	char buffer[1024];
	_u32 rc=dev->Read(buffer, 1024);
	if(rc!=1024)
	{
		Server->Log(L"Error reading data from device ("+pDev+L")", LL_ERROR);
		return NULL;
	}

	Server->destroy(dev);

	if(isNTFS(buffer) )
	{
		Server->Log(L"Filesystem type is ntfs ("+pDev+L")", LL_DEBUG);
		FSNTFS *fs=new FSNTFS(pDev);
		
		/*
		int64 idx=0;
		while(idx<fs->getSize()/fs->getBlocksize())
		{
			std::string b1;
			std::string b2;
			int64 idx_start=idx;
			for(size_t i=0;i<100;++i)
			{
				b1+=nconvert((int)fs->readBlock(idx, NULL));
				b2+=nconvert((int)fs2->readBlock(idx, NULL));
				++idx;
			}
			if(b1!=b2)
			{
				Server->Log(nconvert(idx_start)+" fs1: "+b1, LL_DEBUG);
				Server->Log(nconvert(idx_start)+" fs2: "+b2, LL_DEBUG);
			}
		}*/

		if(fs->hasError())
		{
			Server->Log("NTFS has error", LL_WARNING);
			delete fs;

			Server->Log("Unknown filesystem type", LL_DEBUG);
			FSUnknown *fs2=new FSUnknown(pDev);
			if(fs2->hasError())
			{
				delete fs2;
				return NULL;
			}
			PrintInfo(fs2);
			return fs2;
		}
		PrintInfo(fs);
		return fs;
	}
	else
	{
		Server->Log("Unknown filesystem type", LL_DEBUG);
		FSUnknown *fs=new FSUnknown(pDev);
		if(fs->hasError())
		{
			delete fs;
			return NULL;
		}
		PrintInfo(fs);
		return fs;
	}
}

bool FSImageFactory::isNTFS(char *buffer)
{
	if(buffer[3]=='N' && buffer[4]=='T' && buffer[5]=='F' && buffer[6]=='S')
	{
		return true;
	}
	else
	{
		return false;
	}
}

void FSImageFactory::destroyFilesystem(IFilesystem *fs)
{
	delete fs;
}

IVHDFile *FSImageFactory::createVHDFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize,
	unsigned int pBlocksize, bool fast_mode, ImageFormat format)
{
	switch(format)
	{
	case ImageFormat_VHD:
	case ImageFormat_CompressedVHD:
		return new VHDFile(fn, pRead_only, pDstsize, pBlocksize, fast_mode, format!=ImageFormat_VHD);
	case ImageFormat_RawCowFile:
#ifndef _WIN32
		return new CowFile(fn, pRead_only, pDstsize);
#else
		return NULL;
#endif
	}

	return NULL;
}

IVHDFile *FSImageFactory::createVHDFile(const std::wstring &fn, const std::wstring &parent_fn,
	bool pRead_only, bool fast_mode, ImageFormat format)
{
	switch(format)
	{
	case ImageFormat_VHD:
	case ImageFormat_CompressedVHD:
		return new VHDFile(fn, parent_fn, pRead_only, fast_mode, format!=ImageFormat_VHD);
	case ImageFormat_RawCowFile:
#ifndef _WIN32
		return new CowFile(fn, parent_fn, pRead_only);
#else
		return NULL;
#endif
	}

	return NULL;
}

void FSImageFactory::destroyVHDFile(IVHDFile *vhd)
{
	delete vhd;
}
