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
#include "ClientBitmap.h"

#ifdef _WIN32
namespace
{
	LONG GetStringRegKey(HKEY hKey, const std::string &strValueName, std::string &strValue, const std::string &strDefaultValue)
	{
		strValue = strDefaultValue;
		WCHAR szBuffer[8192];
		DWORD dwBufferSize = sizeof(szBuffer);
		ULONG nError;
		std::wstring rval;
		nError = RegQueryValueExW(hKey, Server->ConvertToWchar(strValueName).c_str(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
		if (ERROR_SUCCESS == nError)
		{
			rval.resize(dwBufferSize/sizeof(wchar_t));
			memcpy(const_cast<wchar_t*>(rval.c_str()), szBuffer, dwBufferSize);
			strValue = Server->ConvertFromWchar(rval);
		}
		return nError;
	}
}
#endif


void PrintInfo(IFilesystem *fs)
{
	Server->Log("FSINFO: blocksize="+convert(fs->getBlocksize())+" size="+convert(fs->getSize())+" has_error="+convert(fs->hasError())+" used_space="+convert(fs->calculateUsedSpace()), LL_DEBUG);
}

IFilesystem *FSImageFactory::createFilesystem(const std::string &pDev, EReadaheadMode read_ahead,
	bool background_priority, std::string orig_letter, IFsNextBlockCallback* next_block_callback)
{
	IFile *dev = Server->openFile(pDev, MODE_READ_DEVICE);

	if(dev==NULL)
	{
		int last_error;
#ifdef _WIN32
		last_error=GetLastError();
#else
		last_error=errno;
#endif
		Server->Log("Error opening device file ("+pDev+") Errorcode: "+convert(last_error), LL_ERROR);
		return NULL;
	}
	char buffer[1024];
	_u32 rc=dev->Read(buffer, 1024);
	if(rc!=1024)
	{
		int last_error;
#ifdef _WIN32
		last_error = GetLastError();
#else
		last_error = errno;
#endif
		Server->Log("Error reading data from device ("+pDev+"). Errorcode: " + convert(last_error), LL_ERROR);
		return NULL;
	}

	Server->destroy(dev);

	if(isNTFS(buffer) )
	{
		Server->Log("Filesystem type is ntfs ("+pDev+")", LL_DEBUG);

		FSNTFS *fs=new FSNTFS(pDev, read_ahead, background_priority, next_block_callback);


#ifdef _WIN32
		if(next(orig_letter, 0, "C"))
		{
			fs->excludeFile(pDev+"\\HIBERFIL.SYS");
		}

		HKEY hKey;
		LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", 0, KEY_READ, &hKey);
		if(lRes == ERROR_SUCCESS)
		{
			std::string tfiles;
			lRes = GetStringRegKey(hKey, "ExistingPageFiles", tfiles, "");
			if(lRes == ERROR_SUCCESS)
			{
				std::vector<std::string> toks;
				std::string sep;
				sep.resize(1);
				sep[0]=0;
				Tokenize(tfiles, toks, sep);
				for(size_t i=0;i<toks.size();++i)
				{
					toks[i]=trim(toks[i].c_str());

					if(toks[i].empty())
						continue;

					toks[i]=trim(toks[i]);

					if(toks[i].find("\\??\\")==0)
					{
						toks[i].erase(0, 4);
					}

					toks[i]=strlower(toks[i]);

					if(next(toks[i], 0, strlower(orig_letter)))
					{
						std::string fs_path=orig_letter;
						if(fs_path.find(":")==std::string::npos)
						{
							fs_path+=":";
						}
						if(fs_path.find("\\")!=std::string::npos)
						{
							fs_path = fs_path.substr(0, fs_path.size()-1);
						}
						if(toks[i].size()>fs_path.size())
						{
							std::string pagefile_shadowcopy = toks[i].substr(fs_path.size());
							fs->excludeFile(pDev+pagefile_shadowcopy);
						}
					}
				}
			}
		}

		char* systemdrive = NULL;
		size_t bufferCount;
		if (_dupenv_s(&systemdrive, &bufferCount, "SystemDrive") == 0)
		{
			std::string s_systemdrive = std::string(systemdrive);

			if (!s_systemdrive.empty()
				&& next(orig_letter, 0, s_systemdrive.substr(0, 1)) )
			{
				fs->excludeFile(pDev + "\\swapfile.sys");
			}
		}
		free(systemdrive);

		//Exclude shadow storage files
		if(pDev.find("HarddiskVolumeShadowCopy") != std::string::npos)
		{
			fs->excludeFiles(pDev + "\\System Volume Information", "{3808876b-c176-4e48-b7ae-04046e6cc752}");
		}
#endif
		
		/*
		int64 idx=0;
		while(idx<fs->getSize()/fs->getBlocksize())
		{
			std::string b1;
			std::string b2;
			int64 idx_start=idx;
			for(size_t i=0;i<100;++i)
			{
				b1+=convert((int)fs->readBlock(idx, NULL));
				b2+=convert((int)fs2->readBlock(idx, NULL));
				++idx;
			}
			if(b1!=b2)
			{
				Server->Log(convert(idx_start)+" fs1: "+b1, LL_DEBUG);
				Server->Log(convert(idx_start)+" fs2: "+b2, LL_DEBUG);
			}
		}*/

		if(fs->hasError())
		{
			Server->Log("NTFS has error", LL_WARNING);
			delete fs;

			Server->Log("Unknown filesystem type", LL_DEBUG);
			FSUnknown *fs2=new FSUnknown(pDev, read_ahead, background_priority, next_block_callback);
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
		FSUnknown *fs=new FSUnknown(pDev, read_ahead, background_priority, next_block_callback);
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

IVHDFile *FSImageFactory::createVHDFile(const std::string &fn, bool pRead_only, uint64 pDstsize,
	unsigned int pBlocksize, bool fast_mode, ImageFormat format)
{
	switch(format)
	{
	case ImageFormat_VHD:
	case ImageFormat_CompressedVHD:
		return new VHDFile(fn, pRead_only, pDstsize, pBlocksize, fast_mode, format!=ImageFormat_VHD);
	case ImageFormat_RawCowFile:
#if !defined(_WIN32) && !defined(__APPLE__)
		return new CowFile(fn, pRead_only, pDstsize);
#else
		return NULL;
#endif
	}
	return NULL;
}

IVHDFile *FSImageFactory::createVHDFile(const std::string &fn, const std::string &parent_fn,
	bool pRead_only, bool fast_mode, ImageFormat format)
{
	switch(format)
	{
	case ImageFormat_VHD:
	case ImageFormat_CompressedVHD:
		return new VHDFile(fn, parent_fn, pRead_only, fast_mode, format!=ImageFormat_VHD);
	case ImageFormat_RawCowFile:
#if !defined(_WIN32) && !defined(__APPLE__)
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

IReadOnlyBitmap * FSImageFactory::createClientBitmap(const std::string & fn)
{
	return new ClientBitmap(fn);
}

IReadOnlyBitmap * FSImageFactory::createClientBitmap(IFile * bitmap_file)
{
	return new ClientBitmap(bitmap_file);
}
