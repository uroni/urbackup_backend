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

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include "file.h"
#include "types.h"
#include "stringtools.h"
#include "Server.h"

#ifdef MODE_LIN

#include <errno.h>
#include <stdint.h>

bool File::Open(std::wstring pfn, int mode)
{
	fn=pfn;
	int flags;
	mode_t imode=S_IRWXU|S_IRWXG;
	if( mode==MODE_READ )
	{
		flags=O_RDONLY;
	}
	else if( mode==MODE_WRITE )
	{
		DeleteFileInt(pfn);
		flags=O_WRONLY|O_CREAT;
	}
	else if( mode==MODE_APPEND )
	{
		flags=O_RDWR | O_APPEND;
	}
	else if( mode==MODE_RW )
	{
		flags=O_RDWR;
	}
	
	struct stat buf;
	if(stat(Server->ConvertToUTF8(fn).c_str(), &buf)==0)
	{
		if(S_ISDIR(buf.st_mode) )
			return false;
	}
	
	
	fd=open64(Server->ConvertToUTF8(fn).c_str(), flags|O_LARGEFILE, imode);
	
	if( fd!=-1 )
	{
		return true;
	}
	else
		return false;
}

bool File::OpenTemporaryFile(const std::wstring &dir)
{
	char *tmpdir=getenv("TMPDIR");
	std::string stmpdir;
	if(tmpdir==NULL )
		stmpdir="/tmp";
	else
	    stmpdir=tmpdir; 

	stmpdir=stmpdir+"/cps.XXXXXX";

	fd=mkstemp((char*)stmpdir.c_str());
	fn=Server->ConvertToUnicode(stmpdir);
	if( fd==-1 )
		return false;
	else
		return true;
}

bool File::Open(void *handle)
{
	fd=(int)((intptr_t)handle);
	return true;
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
	ssize_t r=read(fd, buffer, bsize);
	if( r<0 )
		r=0;
	
	return (_u32)r;
}

_u32 File::Write(const std::string &tw)
{
	return Write( tw.c_str(), (_u32)tw.size() );
}

_u32 File::Write(const char* buffer, _u32 bsize)
{
	ssize_t w=write(fd, buffer, bsize);
	if( w<0 )
	{
		Server->Log("Write failed. errno="+nconvert(errno), LL_DEBUG);
		w=0;
	}
	return (_u32)w;
}

bool File::Seek(_i64 spos)
{
	off64_t ot=lseek64(fd, spos, SEEK_SET);
	if( ot==(off64_t)-1 )
	{
		return false;
	}
	else
	{
		return true;
	}
}

_i64 File::Size(void)
{
	struct stat64 stat_buf;
	fstat64(fd, &stat_buf);

	return stat_buf.st_size;
}

void File::Close()
{
	if( fd!=-1 )
	{
		close( fd );
		fd=-1;
	}
}

#endif
