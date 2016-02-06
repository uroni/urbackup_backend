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

#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Server.h"
#include "file.h"
#include "types.h"
#include "stringtools.h"

#ifdef MODE_LIN

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE    0x1
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE   0x2
#endif

#ifndef SEEK_DATA
#define SEEK_DATA 3
#endif
#ifndef SEEK_HOLE
#define SEEK_HOLE 4
#endif

#if defined(__FreeBSD__) || defined(__APPLE__)
#define open64 open
#define off64_t off_t
#define lseek64 lseek
#define O_LARGEFILE 0
#define stat64 stat
#define fstat64 fstat
#define ftruncate64 ftruncate
#define fallocate64 fallocate
#define pwrite64 pwrite
#define pread64 pread
#else
#include <linux/fs.h>

#if !defined(BLKGETSIZE64) && defined(__i386__) && defined(__x86_64__)
#define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif

#endif

File::File()
	: fd(-1), last_hole_end(0)
{

}

bool File::Open(std::string pfn, int mode)
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
		|| mode==MODE_RW_READNONE
		|| mode==MODE_RW_DEVICE)
	{
		flags=O_RDWR;
		if( mode==MODE_RW_CREATE )
		{
			flags|=O_CREAT;
		}
	}
	
	struct stat buf;
	if(stat((fn).c_str(), &buf)==0)
	{
		if(S_ISDIR(buf.st_mode) )
			return false;
	}
	
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
	
	fd=open64((fn).c_str(), flags|O_LARGEFILE, imode);

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

bool File::OpenTemporaryFile(const std::string &dir, bool first_try)
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
	
	fn=(stmpdir);
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

std::string File::Read(int64 spos, _u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(tr);
	_u32 gc=Read(spos, (char*)ret.c_str(), tr, has_error);
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

_u32 File::Read(int64 spos, char* buffer, _u32 bsize, bool *has_error)
{
	ssize_t r=pread64(fd, buffer, bsize, spos);
	if (r < 0)
	{
		if (has_error) *has_error = true;
		r = 0;
	}

	return (_u32)r;
}

_u32 File::Write(const std::string &tw, bool *has_error)
{
	return Write( tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 File::Write(int64 spos, const std::string &tw, bool *has_error)
{
	return Write(spos, tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 File::Write(const char* buffer, _u32 bsize, bool *has_error)
{
	ssize_t w=write(fd, buffer, bsize);
	if( w<0 )
	{
		Server->Log("Write failed. errno="+convert(errno), LL_DEBUG);
		if (has_error) *has_error = true;
		w=0;
	}
	return (_u32)w;
}

_u32 File::Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error)
{
	ssize_t w=pwrite64(fd, buffer, bsize, spos);
	if( w<0 )
	{
		Server->Log("Write failed. errno="+convert(errno), LL_DEBUG);
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
#if defined(BLKGETSIZE64)
		if(S_ISBLK(stat_buf.st_mode))
		{
			_i64 ret;
			rc = ioctl(fd, BLKGETSIZE64, &ret);
			if(rc==0)
			{
				return ret;
			}
		}
#endif
		
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
#ifdef __APPLE__
	return false;
#elif __FreeBSD__
	struct flock64 s;
	s.l_whence = SEEK_SET;
	s.l_start = spos;
	s.l_len = size;
	int rc = fcntl(fd, F_FREESP64, &s);
	return rc == 0;
#else
	int rc = fallocate64(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, spos, size);

	return rc == 0;
#endif
}

bool File::Sync()
{
	return fsync(fd)==0;
}

bool File::Resize(int64 new_size)
{
	return ftruncate64(fd, new_size) == 0;
}

void File::resetSparseExtentIter()
{
	last_hole_end = 0;
}

namespace
{
	class ResetCur
	{
	public:
		ResetCur(int fd, off64_t pos)
			: fd(fd), pos(pos)
		{}

		~ResetCur()
		{
			lseek64(fd, pos, SEEK_SET);
		}

	private:
		int fd;
		off64_t pos;
	};
}

IFsFile::SSparseExtent File::nextSparseExtent()
{
	if (last_sparse_pos == -1)
	{
		return SSparseExtent();
	}

	off64_t curr_pos = lseek64(fd, 0, SEEK_CUR);

	if (curr_pos == -1)
	{
		return SSparseExtent();
	}

	ResetCur reset_cur(fd, curr_pos);
	
	off64_t next_hole_start = lseek64(fd, last_sparse_pos, SEEK_HOLE);

	if (next_hole_start == -1)
	{
		last_sparse_pos = -1;
		return SSparseExtent();
	}

	_i64 fsize = Size();

	if (next_hole_start == fsize)
	{
		last_sparse_pos = -1;
		return SSparseExtent();
	}

	last_sparse_pos = lseek64(fd, next_hole_start, SEEK_DATA);

	if (last_sparse_pos == -1)
	{
		return SSparseExtent(next_hole_start, fsize - next_hole_start);
	}

	return SSparseExtent(next_hole_start, last_sparse_pos - next_hole_start);
}

#endif
