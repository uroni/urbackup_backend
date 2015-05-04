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

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include "Server.h"
#include "file.h"
#include "types.h"
#include "stringtools.h"

#ifdef MODE_LIN

#include <errno.h>
#include <stdint.h>

#include <sys/fcntl.h>
#include <sys/ioctl.h>

#if defined(__FreeBSD__) || defined(__APPLE__)
#define open64 open
#define off64_t off_t
#define lseek64 lseek
#define O_LARGEFILE 0
#define stat64 stat
#define fstat64 fstat
#else
#include <linux/fs.h>
#endif

File::File()
	: fd(-1)
{

}

bool File::Open(std::wstring pfn, int mode)
{
	fn=pfn;
	int flags=0;
	mode_t imode=S_IRWXU|S_IRWXG;
	if( mode==MODE_READ
		|| mode==MODE_READ_DEVICE
		|| mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP)
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
	else if( mode==MODE_RW
		|| mode==MODE_RW_SEQUENTIAL
		|| mode==MODE_RW_CREATE
		|| mode==MODE_RW_READNONE )
	{
		flags=O_RDWR;
		if( mode==MODE_RW_CREATE )
		{
			flags|=O_CREAT;
		}
	}
	
	struct stat buf;
	if(stat(Server->ConvertToUTF8(fn).c_str(), &buf)==0)
	{
		if(S_ISDIR(buf.st_mode) )
			return false;
	}
	
	
	fd=open64(Server->ConvertToUTF8(fn).c_str(), flags|O_LARGEFILE, imode);

#ifdef __linux__
	if(mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode==MODE_RW_SEQUENTIAL)
	{
		posix_fadvise64(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	}

	if( mode==MODE_RW_READNONE )
	{
		posix_fadvise64(fd, 0, 0, POSIX_FADV_DONTNEED);
	}
#endif
	
	if( fd!=-1 )
	{
		return true;
	}
	else
		return false;
}

bool File::OpenTemporaryFile(const std::wstring &dir, bool first_try)
{
	char *tmpdir=getenv("TMPDIR");
	std::string stmpdir;
	if(tmpdir==NULL )
		stmpdir="/tmp";
	else
	    stmpdir=tmpdir; 

	stmpdir=stmpdir+"/cps.XXXXXX";

	mode_t cur_umask = umask(S_IRWXO | S_IRWXG); 
	fd=mkstemp((char*)stmpdir.c_str());
	umask(cur_umask);
	
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

std::string File::Read(_u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read((char*)ret.c_str(), tr, has_error);
	if( gc<tr )
		ret.resize( gc );
	
	return ret;
}

_u32 File::Read(char* buffer, _u32 bsize, bool *has_error)
{
	ssize_t r=read(fd, buffer, bsize);
	if( r<0 )
	{
		if(has_error) *has_error=true;
		r=0;
	}
	
	return (_u32)r;
}

_u32 File::Write(const std::string &tw, bool *has_error)
{
	return Write( tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 File::Write(const char* buffer, _u32 bsize, bool *has_error)
{
	ssize_t w=write(fd, buffer, bsize);
	if( w<0 )
	{
		Server->Log("Write failed. errno="+nconvert(errno), LL_DEBUG);
		if(has_error) *has_error=true;
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
	int rc = fstat64(fd, &stat_buf);
	
	if(rc==0)
	{
		if(S_ISBLK(stat_buf.st_mode))
		{
			_i64 ret;
			rc = ioctl(fd, BLKGETSIZE64, &ret);
			if(rc==0)
			{
				return ret;
			}
		}
		
		return stat_buf.st_size;	
	}
	else
	{
		return -1;
	}
}

_i64 File::RealSize(void)
{
	struct stat64 stat_buf;
	fstat64(fd, &stat_buf);

	return stat_buf.st_blocks*512;
}

void File::Close()
{
	if( fd!=-1 )
	{
		close( fd );
		fd=-1;
	}
}

bool File::PunchHole( _i64 spos, _i64 size )
{
	int rc = fallocate64(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, spos, size);

	if(rc==0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

#endif
