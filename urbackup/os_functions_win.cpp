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

#include "os_functions.h"
#include "../stringtools.h"
//#define _WIN32_WINNT 0x0500
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <algorithm>

void getMousePos(int &x, int &y)
{
	POINT mousepos;
	GetCursorPos(&mousepos);
	x=mousepos.x;
	y=mousepos.y;
}

std::vector<SFile> getFiles(const std::wstring &path)
{
	std::vector<SFile> tmp;
	HANDLE fHandle;
	WIN32_FIND_DATAW wfd;
	std::wstring tpath=path;
	if(!tpath.empty() && tpath[tpath.size()-1]=='\\' ) tpath.erase(path.size()-1, 1);
	fHandle=FindFirstFileW((tpath+L"\\*").c_str(),&wfd); 
	if(fHandle==INVALID_HANDLE_VALUE)
		return tmp;

	do
	{
		if( !(wfd.dwFileAttributes &FILE_ATTRIBUTE_REPARSE_POINT && wfd.dwFileAttributes &FILE_ATTRIBUTE_DIRECTORY) )
		{
			SFile f;
			f.name=wfd.cFileName;
			if(f.name==L"." || f.name==L".." )
				continue;
			f.isdir=(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)>0;			
			LARGE_INTEGER lwt;
			lwt.HighPart=wfd.ftLastWriteTime.dwHighDateTime;
			lwt.LowPart=wfd.ftLastWriteTime.dwLowDateTime;
			f.last_modified=lwt.QuadPart;
			LARGE_INTEGER size;
			size.HighPart=wfd.nFileSizeHigh;
			size.LowPart=wfd.nFileSizeLow;
			f.size=size.QuadPart;
			tmp.push_back(f);		
		}
	}
	while (FindNextFileW(fHandle,&wfd) );
	FindClose(fHandle);

	std::sort(tmp.begin(), tmp.end());

	return tmp;
}

void removeFile(const std::wstring &path)
{
	_unlink(wnarrow(path).c_str());
}

void moveFile(const std::wstring &src, const std::wstring &dst)
{
	rename(wnarrow(src).c_str(), wnarrow(dst).c_str() );
}

bool isDirectory(const std::wstring &path)
{
        DWORD attrib = GetFileAttributesW(path.c_str());

        if ( attrib == 0xFFFFFFFF || !(attrib & FILE_ATTRIBUTE_DIRECTORY) )
        {
                return false;
        }
        else
        {
                return true;
        }
}

int64 os_atoi64(const std::string &str)
{
	return _atoi64(str.c_str());
}

bool os_create_dir(const std::wstring &dir)
{
	return CreateDirectoryW(dir.c_str(), NULL)!=0;
}

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname)
{
	BOOL r=CreateHardLinkW(linkname.c_str(), fname.c_str(), NULL);
	if(!r)
	{
		int err=GetLastError();
		if(err==1142) //TOO MANY LINKS
		{
			int blub=55; //Debugging
		}
	}
	return r!=0;
}

int64 os_free_space(const std::wstring &path)
{
	std::wstring cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='\\')
		cp+='\\';

	ULARGE_INTEGER li;
	BOOL r=GetDiskFreeSpaceExW(path.c_str(), &li, NULL, NULL);
	if(r!=0)
		return li.QuadPart;
	else
		return -1;
}

bool os_directory_exists(const std::wstring &path)
{
	return isDirectory(path);
}

bool os_remove_nonempty_dir(const std::wstring &path)
{
	WIN32_FIND_DATAW wfd; 
	HANDLE hf=FindFirstFileW((path+L"\\*").c_str(), &wfd);
	BOOL b=true;
	while( b )
	{
		if( (std::wstring)wfd.cFileName!=L"." && (std::wstring)wfd.cFileName!=L".." )
		{
			if(	wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				os_remove_nonempty_dir(path+L"\\"+wfd.cFileName);
			}
			else
			{
				DeleteFileW((path+L"\\"+wfd.cFileName).c_str());
			}
		}
		b=FindNextFileW(hf,&wfd);			
	}

	FindClose(hf);
	RemoveDirectoryW(path.c_str());
	return true;
}

std::wstring os_file_sep(void)
{
	return L"\\";
}

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname)
{
#if (_WIN32_WINNT >= 0x0600)
	DWORD flags=0;
	if(isDirectory(target))
		flags|=SYMBOLIC_LINK_FLAG_DIRECTORY;

	return CreateSymbolicLink(lname.c_str(), target.c_str(), flags)!=0;
#else
	return true;
#endif
}

bool os_lookuphostname(std::string pServer, unsigned int *dest)
{
	const char* host=pServer.c_str();
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE)
	{
        *dest = addr;
    }
	else
	{
		hostent* hp = gethostbyname(host);
        if (hp != 0)
		{
			in_addr tmp;
			memcpy(&tmp, hp->h_addr,  hp->h_length );
			*dest=tmp.s_addr;
		}
		else
		{
			return false;
		}
	}
	return true;
}

std::wstring os_file_prefix(void)
{
	return L"\\\\?\\";
}