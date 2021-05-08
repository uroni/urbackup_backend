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

#include "Server.h"
#include "file_memory.h"

#include <algorithm>
#include <assert.h>
#ifndef _WIN32
#include <unistd.h>
#include <memory.h>
#include <sys/mman.h>
#include <pthread.h>
#endif
#include "stringtools.h"

int CMemoryFile::page_size = 0;

CMemoryFile::CMemoryFile(const std::string& name, bool mlock_mem)
	:mutex(Server->createSharedMutex()),
	mlock_mem(mlock_mem), name(name), mprotect_mutex(Server->createMutex()), memory_protected(0)
{
	pos=0;

#ifndef _WIN32
	if (page_size == 0)
	{
		page_size = sysconf(_SC_PAGE_SIZE);

		if (page_size <= 0)
		{
			Server->Log("Invalid page size " + convert(page_size) + " errno " + convert((int64)errno), LL_ERROR);
			page_size = 4096;
		}
	}
#endif
}

CMemoryFile::~CMemoryFile()
{
#ifndef _WIN32
	if (mlock_mem && !data.empty())
		munlock(data.data(), data.size());
#endif
}

std::string CMemoryFile::Read(_u32 tr, bool *has_error)
{
	std::string ret = Read(pos, tr, has_error);
	pos += ret.size();
	return ret;
}

_u32 CMemoryFile::Read(char* buffer, _u32 bsize, bool *has_error)
{
	_u32 read = Read(pos, buffer, bsize, has_error);
	pos += read;
	return read;
}

_u32 CMemoryFile::Write(const std::string &tw, bool *has_error)
{
	return Write(tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 CMemoryFile::Write(const char* buffer, _u32 bsize, bool *has_error)
{
	_u32 written = Write(pos, buffer, bsize, has_error);
	pos += written;
	return written;
}
bool CMemoryFile::Seek(_i64 spos)
{
	if(spos>=0)
	{
		pos=(size_t)spos;
		return true;
	}
	else
	{
		return false;
	}
}

_i64 CMemoryFile::Size(void)
{
	IScopedReadLock lock(mutex.get());
	return data.size();
}

_i64 CMemoryFile::RealSize()
{
	return Size();
}

std::string CMemoryFile::getFilename()
{
	return name;
}

void CMemoryFile::resetSparseExtentIter()
{
}

IFsFile::SSparseExtent CMemoryFile::nextSparseExtent()
{
	return SSparseExtent();
}

bool CMemoryFile::Resize(int64 new_size, bool set_sparse)
{
	if (new_size < 0)
	{
		return false;
	}
	IScopedWriteLock lock(mutex.get());
	unprotect_mem();
	data.resize(new_size);
#ifndef _WIN32
	if(mlock_mem)
		mlock(data.data(), data.size());
#endif
	protect_mem();
	return true;
}

std::vector<IFsFile::SFileExtent> CMemoryFile::getFileExtents(int64 starting_offset, int64 block_size, bool & more_data, unsigned int flags)
{
	return std::vector<IFsFile::SFileExtent>();
}

IFsFile::os_file_handle CMemoryFile::getOsHandle(bool release_handle)
{
	return os_file_handle();
}

char * CMemoryFile::getDataPtr()
{
	return data.data();
}

void CMemoryFile::protect_mem()
{
#ifndef _WIN32
	IScopedLock lock(mprotect_mutex.get());
	if (memory_protected<=1)
	{
		if (!data.empty() 
			&& mprotect(data.data(), data.size(), PROT_READ) != 0)
		{
			Server->Log("mprotect(r) failed at " + convert((int64)data.data()) + " size " + convert(data.size()) + " errno " + convert((int64)errno), LL_WARNING);
		}

		--memory_protected;
	}
	else
	{
		--memory_protected;
	}
#endif
}

void CMemoryFile::unprotect_mem()
{
#ifndef _WIN32
	IScopedLock lock(mprotect_mutex.get());
	if (memory_protected==0)
	{
		if (!data.empty()
			&& mprotect(data.data(), data.size(), PROT_READ|PROT_WRITE) != 0)
		{
			Server->Log("mprotect(w) failed at " + convert((int64)data.data()) + " size " + convert(data.size()) + " errno " + convert((int64)errno), LL_WARNING);
		}

		++memory_protected;
	}
	else
	{
		++memory_protected;
	}
#endif
}

IVdlVolCache* CMemoryFile::createVdlVolCache()
{
	return nullptr;
}

int64 CMemoryFile::getValidDataLength(IVdlVolCache* vol_cache)
{
	return -1;
}

std::string CMemoryFile::Read(int64 spos, _u32 tr, bool * has_error)
{
	IScopedReadLock lock(mutex.get());

	if (spos >= static_cast<int64>(data.size()))
		return "";

	size_t rtr = static_cast<size_t>((std::min)(static_cast<int64>(tr), static_cast<int64>(data.size()) - spos));
	std::string ret;
	ret.assign(&data[spos], rtr);

	return ret;
}

_u32 CMemoryFile::Read(int64 spos, char * buffer, _u32 bsize, bool * has_error)
{
	IScopedReadLock lock(mutex.get());

	if (spos >= static_cast<int64>(data.size()))
		return 0;


	size_t rtr = static_cast<size_t>((std::min)(static_cast<int64>(bsize), static_cast<int64>(data.size()) - spos));
	memcpy(buffer, &data[spos], rtr);

	return (_u32)rtr;
}

_u32 CMemoryFile::Write(int64 spos, const std::string & tw, bool * has_error)
{
	return Write(spos, tw.data(), static_cast<_u32>(tw.size()), has_error);
}

_u32 CMemoryFile::Write(int64 spos, const char * buffer, _u32 bsize, bool * has_error)
{
	IScopedReadLock lock(mutex.get());
	if (spos + bsize>static_cast<int64>(data.size()))
	{
		lock.relock(NULL);
		IScopedWriteLock wrlock(mutex.get());
		unprotect_mem();
		if (spos + bsize > static_cast<int64>(data.size()))
		{
#ifndef _WIN32
			if (mlock_mem && !data.empty())
				munlock(data.data(), data.size());
#endif
			data.resize(spos + bsize);
#ifndef _WIN32
			if (mlock_mem)
				mlock(data.data(), data.size());
#endif
		}
		memcpy(&data[spos], buffer, bsize);
		protect_mem();
		return bsize;
	}
	else
	{
		unprotect_mem();
		memcpy(&data[spos], buffer, bsize);
		protect_mem();
		return bsize;
	}
}

bool CMemoryFile::PunchHole( _i64 spos, _i64 size )
{
	return false;
}

bool CMemoryFile::Sync()
{
	return false;
}
