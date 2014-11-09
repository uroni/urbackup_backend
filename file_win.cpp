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

#include "Server.h"
#include "file.h"
#include "types.h"
#include "stringtools.h"
#include <sstream>

#ifdef MODE_WIN

size_t File::tmp_file_index = 0;
IMutex* File::index_mutex = NULL;
std::wstring File::random_prefix;

File::File()
	: hfile(INVALID_HANDLE_VALUE)
{

}

bool File::Open(std::wstring pfn, int mode)
{
	fn=pfn;
	DWORD dwCreationDisposition;
	DWORD dwDesiredAccess;
	DWORD dwShareMode=FILE_SHARE_READ;
	if( mode==MODE_READ
		|| mode==MODE_READ_DEVICE
		|| mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP)
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
	else if( mode==MODE_APPEND )
	{
		dwCreationDisposition=OPEN_EXISTING;
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}
	else if( mode==MODE_TEMP )
	{
		dwCreationDisposition=CREATE_NEW;
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}
	else if( mode==MODE_RW 
		|| mode==MODE_RW_SEQUENTIAL
		|| mode==MODE_RW_CREATE
		|| mode==MODE_RW_READNONE)
	{
		if(mode==MODE_RW
			|| mode==MODE_RW_SEQUENTIAL
			|| mode==MODE_RW_READNONE)
		{
			dwCreationDisposition=OPEN_EXISTING;
		}
		else
		{
			dwCreationDisposition=CREATE_NEW;
		}
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}

	if(mode==MODE_READ_DEVICE)
	{
		dwShareMode|=FILE_SHARE_WRITE;
	}

	DWORD flags=FILE_ATTRIBUTE_NORMAL;
	if(mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode==MODE_RW_SEQUENTIAL)
	{
		flags|=FILE_FLAG_SEQUENTIAL_SCAN;
	}
	if(mode==MODE_READ_SEQUENTIAL_BACKUP)
	{
		flags|=FILE_FLAG_BACKUP_SEMANTICS;
	}
	
	hfile=CreateFileW( fn.c_str(), dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, flags, NULL );

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
		hfile=NULL;
		return false;
	}
}

bool File::OpenTemporaryFile(const std::wstring &tmpdir, bool first_try)
{
	std::wostringstream filename;

	if(tmpdir.empty())
	{
		wchar_t tmpp[MAX_PATH];
		DWORD l;
		if((l=GetTempPathW(MAX_PATH, tmpp))==0 || l>MAX_PATH )
		{
			wcscpy_s(tmpp,L"C:\\");
		}
		
		filename << tmpp;
	}
	else
	{
		filename << tmpdir;

		if(tmpdir[tmpdir.size()-1]!='\\')
		{
			filename << L"\\";
		}
	}

	filename << L"urb" << random_prefix << L"-" << std::hex;

	{
		IScopedLock lock(index_mutex);
		filename << ++tmp_file_index;
	}

	filename << L".tmp";

	if(!Open(filename.str(), MODE_TEMP))
	{
		if(first_try)
		{
			Server->Log(L"Creating temporary file at \"" + filename.str()+L"\" failed. Creating directory \""+tmpdir+L"\"...", LL_WARNING);
			BOOL b = CreateDirectoryW(tmpdir.c_str(), NULL);

			if(b)
			{
				return OpenTemporaryFile(tmpdir, false);
			}
			else
			{
				Server->Log(L"Creating directory \""+tmpdir+L"\" failed.", LL_WARNING);
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	else
	{
		return true;
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

_i64 File::RealSize()
{
	return Size();
}

void File::Close()
{
	if( hfile!=NULL )
	{
		BOOL b=CloseHandle( hfile );
		hfile=NULL;
	}
}

void File::init_mutex()
{
	index_mutex = Server->createMutex();

	std::string rnd;
	rnd.resize(8);
	unsigned int timesec = static_cast<unsigned int>(Server->getTimeSeconds());
	memcpy(&rnd[0], &timesec, sizeof(timesec));
	Server->randomFill(&rnd[4], 4);

	random_prefix = widen(bytesToHex(reinterpret_cast<unsigned char*>(&rnd[0]), rnd.size()));
}

void File::destroy_mutex()
{
	Server->destroy(index_mutex);
}

#endif
