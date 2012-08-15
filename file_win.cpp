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
#include "file.h"
#include "types.h"
#include "stringtools.h"
#ifdef _DEBUG
#include "Server.h"
#endif

#ifdef MODE_WIN

File::File()
	: hfile(INVALID_HANDLE_VALUE)
{

}

bool File::Open(std::wstring pfn, int mode)
{
	fn=pfn;
	DWORD dwCreationDisposition;
	DWORD dwDesiredAccess;
	if( mode==MODE_READ )
	{
		dwCreationDisposition=OPEN_EXISTING;
		dwDesiredAccess=GENERIC_READ;
	}
	else if( mode==MODE_WRITE )
	{
		DeleteFileInt(pfn);
		dwCreationDisposition=CREATE_NEW;
		dwDesiredAccess=GENERIC_WRITE;
	}
	else if( mode==MODE_APPEND|| mode==MODE_TEMP )
	{
		dwCreationDisposition=OPEN_EXISTING;
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}
	else if( mode==MODE_RW )
	{
		dwCreationDisposition=OPEN_EXISTING;
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}
	
	hfile=CreateFileW( fn.c_str(), dwDesiredAccess, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL );

	if( hfile!=INVALID_HANDLE_VALUE )
	{
		if( mode==MODE_APPEND )
		{
			Seek( Size() );
		}
		return true;
	}
	else
	{
#ifdef _DEBUG
		Server->Log("EC: "+nconvert((int)GetLastError()));
#endif
		hfile=NULL;
		return false;
	}
}

bool File::OpenTemporaryFile(const std::wstring &tmpdir)
{
	if(tmpdir.empty())
	{
		wchar_t tmpp[MAX_PATH];
		DWORD l;
		if((l=GetTempPathW(MAX_PATH, tmpp))==0 || l>MAX_PATH )
		{
			wcscpy_s(tmpp,L"C:\\");
		}

		wchar_t filename[MAX_PATH];
		if( GetTempFileNameW(tmpp, L"urbackup.t", 0, filename)==0 )
		{
			hfile=NULL;
			int err=GetLastError();
			return false;
		}

		return Open(filename, MODE_TEMP);
	}
	else
	{
		wchar_t filename[MAX_PATH];
		if( GetTempFileNameW(tmpdir.c_str(), L"urbackup.t", 0, filename)==0 )
		{
			hfile=NULL;
			int err=GetLastError();
			return false;
		}

		return Open(filename, MODE_TEMP);
	}
}

bool File::Open(void *handle)
{
	hfile=(HANDLE)handle;
	if( hfile!=INVALID_HANDLE_VALUE )
	{
		return true;
	}
	else
	{
		return false;
	}
}

std::string File::Read(_u32 tr)
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read((char*)ret.c_str(), tr);
	if( gc<tr )
		ret.resize( gc );
	
	return ret;
}

_u32 File::Read(char* buffer, _u32 bsize)
{
	DWORD read;
	BOOL b=ReadFile(hfile, buffer, bsize, &read, NULL );
#ifdef _DEBUG
	if(b==FALSE)
	{
		int err=GetLastError();
		Server->Log("Read error: "+nconvert(err));
	}
	/*if(read!=bsize)
	{
		int err=GetLastError();
		Server->Log("Read error: "+nconvert(err));
	}*/
#endif
	return (_u32)read;
}

_u32 File::Write(const std::string &tw)
{
	return Write( tw.c_str(), (_u32)tw.size() );
}

_u32 File::Write(const char* buffer, _u32 bsize)
{
	DWORD written;
	WriteFile(hfile, buffer, bsize, &written, NULL);
	return written;
}

bool File::Seek(_i64 spos)
{
	LARGE_INTEGER tmp;
	tmp.QuadPart=spos;
	if( SetFilePointerEx(hfile, tmp, NULL, FILE_BEGIN) == FALSE )
	{
		int err=GetLastError();
		return false;
	}
	else
		return true;
}

_i64 File::Size(void)
{
	LARGE_INTEGER fs;
	GetFileSizeEx(hfile, &fs);

	return fs.QuadPart;
}

void File::Close()
{
	if( hfile!=NULL )
	{
		BOOL b=CloseHandle( hfile );
		hfile=NULL;
	}
}

#endif
