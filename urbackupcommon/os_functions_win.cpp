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
#ifndef OS_FUNC_NO_SERVER
#include "../Interface/Server.h"
#endif
#ifdef _WIN_PRE_VISTA
#define _WIN32_WINNT 0x0500
#endif
#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <algorithm>

#include <io.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <time.h>

#define REPARSE_MOUNTPOINT_HEADER_SIZE   8

typedef struct {
  DWORD ReparseTag;
  DWORD ReparseDataLength;
  WORD Reserved;
  WORD ReparseTargetLength;
  WORD ReparseTargetMaximumLength;
  WORD Reserved1;
  WCHAR ReparseTarget[1];
} REPARSE_MOUNTPOINT_DATA_BUFFER, *PREPARSE_MOUNTPOINT_DATA_BUFFER;

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

#ifndef OS_FUNC_NO_SERVER
void removeFile(const std::wstring &path)
{
	_unlink(Server->ConvertToUTF8(path).c_str());
}

void moveFile(const std::wstring &src, const std::wstring &dst)
{
	rename(Server->ConvertToUTF8(src).c_str(), Server->ConvertToUTF8(dst).c_str() );
}
#endif

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

bool os_create_dir(const std::string &dir)
{
	return CreateDirectoryA(dir.c_str(), NULL)!=0;
}

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname, bool use_ioref)
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

bool os_remove_symlink_dir(const std::wstring &path)
{
	return RemoveDirectoryW(path.c_str())!=FALSE;
}

bool os_remove_dir(const std::string &path)
{
	return RemoveDirectoryA(path.c_str())!=FALSE;
}

std::wstring os_file_sep(void)
{
	return L"\\";
}

std::string os_file_sepn(void)
{
	return "\\";
}

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname)
{
#ifdef USE_SYMLINK
#if (_WIN32_WINNT >= 0x0600)
	DWORD flags=0;
	if(isDirectory(target))
		flags|=SYMBOLIC_LINK_FLAG_DIRECTORY;

	DWORD rc=CreateSymbolicLink(lname.c_str(), target.c_str(), flags);
	if(rc==FALSE)
	{
#ifndef OS_FUNC_NO_SERVER
		Server->Log(L"Creating symbolic link from \""+lname+L"\" to \""+target+L"\" failed with error "+convert((int)GetLastError()), LL_ERROR);
#endif
	}
	return rc!=0;
#else
	return true;
#endif
#else
	bool ret=false;
	std::wstring wtarget=target;
	HANDLE hJunc=INVALID_HANDLE_VALUE;
	char *buf=NULL;

	if(!wtarget.empty() && wtarget[0]!='\\')
		wtarget=L"\\??\\"+wtarget;
	if(!wtarget.empty() && wtarget[target.size()-1]!='\\')
		wtarget+=L"\\";

	if(!CreateDirectoryW(lname.c_str(), NULL) )
	{
		goto cleanup;
	}

	hJunc=CreateFileW(lname.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if(hJunc==INVALID_HANDLE_VALUE)
		goto cleanup;

	size_t bsize=sizeof(REPARSE_MOUNTPOINT_DATA_BUFFER) + (wtarget.size()+1) * sizeof(wchar_t)+30;
	buf=new char[bsize];
	memset(buf, 0, bsize);

	REPARSE_MOUNTPOINT_DATA_BUFFER *rb=(REPARSE_MOUNTPOINT_DATA_BUFFER*)buf;
	rb->ReparseTag=IO_REPARSE_TAG_MOUNT_POINT;
	rb->ReparseTargetMaximumLength=(DWORD)(wtarget.size()*sizeof(wchar_t));
	rb->ReparseDataLength=rb->ReparseTargetMaximumLength+12;
	rb->ReparseTargetLength=rb->ReparseTargetMaximumLength;
	memcpy(rb->ReparseTarget, wtarget.c_str(), rb->ReparseTargetMaximumLength);

	DWORD bytes_ret;
	if(!DeviceIoControl(hJunc, FSCTL_SET_REPARSE_POINT, rb, rb->ReparseDataLength+REPARSE_MOUNTPOINT_HEADER_SIZE, NULL, 0, &bytes_ret, NULL) )
	{
		goto cleanup;
	}
	ret=true;

cleanup:
	if(!ret)
	{
		Server->Log("Creating junction failed. Last error="+nconvert((int)GetLastError()), LL_ERROR);
	}
	delete []buf;
	if(hJunc!=INVALID_HANDLE_VALUE)
		CloseHandle(hJunc);
	if(!ret)
	{
		RemoveDirectoryW(lname.c_str());
	}
	return ret;
#endif
}

#ifndef OS_FUNC_NO_NET
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
#endif

std::wstring os_file_prefix(std::wstring path)
{
	if(path.size()>=2 && path[0]=='\\' && path[1]=='\\' )
		return path;
	else
		return L"\\\\?\\"+path;
}

bool os_file_truncate(const std::wstring &fn, int64 fsize)
{
	int fh;
	if( _wsopen_s ( &fh, fn.c_str(), _O_RDWR | _O_CREAT, _SH_DENYNO,
            _S_IREAD | _S_IWRITE ) == 0 )
	{
		if( _chsize_s( fh, fsize ) != 0 )
		{
			_close( fh );
			return false;
		}
		_close( fh );
		return true;
	}
	else
	{
		return false;
	}
}

std::string os_strftime(std::string fs)
{
	time_t rawtime;		
	char buffer [100];
	time ( &rawtime );
	struct tm  timeinfo;
	localtime_s(&timeinfo, &rawtime);
	strftime (buffer,100,fs.c_str(),&timeinfo);
	std::string r(buffer);
	return r;
}

bool os_create_dir_recursive(std::wstring fn)
{
	if(fn.empty())
		return false;

	bool b=os_create_dir(fn);
	if(!b)
	{
		b=os_create_dir_recursive(ExtractFilePath(fn));
		if(!b)
			return false;

		return os_create_dir(fn);
	}
	else
	{
		return true;
	}
}

std::wstring os_get_final_path(std::wstring path)
{
#if (_WIN32_WINNT >= 0x0600)
	std::wstring ret;

	HANDLE hFile = CreateFileW(path.c_str(),               
                       GENERIC_READ,          
                       FILE_SHARE_READ,       
                       NULL,                  
                       OPEN_EXISTING,         
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, 
                       NULL);

	if( hFile==INVALID_HANDLE_VALUE )
	{
#ifndef OS_FUNC_NO_SERVER
		Server->Log(L"Could not open path in os_get_final_path for \""+path+L"\"", LL_ERROR);
#endif
		return path;
	}

	DWORD dwBufsize = GetFinalPathNameByHandleW( hFile, NULL, 0, VOLUME_NAME_DOS );

	if(dwBufsize==0)
	{
#ifndef OS_FUNC_NO_SERVER
		Server->Log(L"Error getting path size in in os_get_final_path error="+convert((int)GetLastError())+L" for \""+path+L"\"", LL_ERROR);
#endif
		CloseHandle(hFile);
		return path;
	}

	ret.resize(dwBufsize+1);

	DWORD dwRet = GetFinalPathNameByHandleW( hFile, (LPWSTR)ret.c_str(), dwBufsize, VOLUME_NAME_DOS );

	CloseHandle(hFile);

	if(dwRet==0)
	{
#ifndef OS_FUNC_NO_SERVER
		Server->Log("Error getting path in in os_get_final_path error="+nconvert((int)GetLastError()), LL_ERROR);
#endif
	}
	else if(dwRet<ret.size())
	{
		ret.resize(dwRet);
		if(ret.find(L"\\\\?\\")==0)
		{
			ret.erase(0,4);
		}
		/*if(ret.size()>=2 && ret[ret.size()-2]=='.' && ret[ret.size()-1]=='.' )
		{
			ret.resize(ret.size()-2);
		}*/
		return ret;
	}
	else
	{
#ifndef OS_FUNC_NO_SERVER
		Server->Log("Error getting path (buffer too small) in in os_get_final_path error="+nconvert((int)GetLastError()), LL_ERROR);
#endif
	}

	return path;
#else
	return path;
#endif
}

#ifndef OS_FUNC_NO_SERVER
bool os_rename_file(std::wstring src, std::wstring dst)
{
	DeleteFileW(dst.c_str());
	BOOL rc=MoveFileW(src.c_str(), dst.c_str());
#ifdef _DEBUG
	if(rc==0)
	{
		Server->Log("MoveFileW error: "+nconvert((int)GetLastError()), LL_ERROR);
	}
#endif
	return rc!=0;
}
#endif