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

#include "file.h"
#include <sstream>
#include "utf8.h"
#include <string>

#ifdef MODE_WIN

File::File()
	: hfile(INVALID_HANDLE_VALUE), is_sparse(false), more_extents(true), curr_extent(0), last_sparse_pos(0)
{

}

bool File::Open(std::string pfn, int mode)
{
	if(mode==MODE_RW_DIRECT)
		mode = MODE_RW;
	if(mode==MODE_RW_CREATE_DIRECT)
		mode = MODE_RW_CREATE;
	
	fn=pfn;
	DWORD dwCreationDisposition;
	DWORD dwDesiredAccess;
	DWORD dwShareMode=FILE_SHARE_READ;
	if( mode==MODE_READ
		|| mode==MODE_READ_DEVICE
		|| mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode== MODE_READ_DEVICE_OVERLAPPED)
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
		|| mode==MODE_RW_READNONE
		|| mode== MODE_RW_DEVICE
		|| mode==MODE_RW_RESTORE
		|| mode==MODE_RW_CREATE_RESTORE
		|| mode== MODE_RW_CREATE_DEVICE
		|| mode== MODE_RW_CREATE_DELETE
		|| mode==MODE_RW_DELETE)
	{
		if(mode==MODE_RW
			|| mode==MODE_RW_SEQUENTIAL
			|| mode==MODE_RW_READNONE
			|| mode== MODE_RW_DEVICE
			|| mode==MODE_RW_RESTORE
			|| mode==MODE_RW_DELETE)
		{
			dwCreationDisposition=OPEN_EXISTING;
		}
		else
		{
			dwCreationDisposition=OPEN_ALWAYS;
		}
		dwDesiredAccess=GENERIC_WRITE | GENERIC_READ;
	}

	if(mode==MODE_READ_DEVICE
		|| mode== MODE_RW_DEVICE
		|| mode== MODE_RW_DELETE
		|| mode== MODE_RW_CREATE_DEVICE
		|| mode== MODE_RW_CREATE_DELETE
		|| mode== MODE_READ_DEVICE_OVERLAPPED)
	{
		dwShareMode|=FILE_SHARE_WRITE;
	}

	if (mode == MODE_RW_CREATE_DELETE
		|| mode == MODE_RW_DELETE)
	{
		dwShareMode |= FILE_SHARE_DELETE;
	}

	DWORD flags=FILE_ATTRIBUTE_NORMAL;
	if(mode==MODE_READ_SEQUENTIAL
		|| mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode==MODE_RW_SEQUENTIAL)
	{
		flags|=FILE_FLAG_SEQUENTIAL_SCAN;
	}
	if(mode==MODE_READ_SEQUENTIAL_BACKUP
		|| mode==MODE_RW_RESTORE
		|| mode==MODE_RW_CREATE_RESTORE)
	{
		flags|=FILE_FLAG_BACKUP_SEMANTICS;
	}
	if (mode == MODE_READ_DEVICE_OVERLAPPED)
	{
		flags |= FILE_FLAG_OVERLAPPED| FILE_FLAG_NO_BUFFERING;
	}
	
	hfile=CreateFileW( ConvertToWchar(fn).c_str(), dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, flags, NULL );

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
		DWORD err = GetLastError();
		return false;
	}
}

bool File::Open(void *handle, const std::string& pFilename)
{
	hfile=(HANDLE)handle;
	fn = pFilename;
	if( hfile!=INVALID_HANDLE_VALUE )
	{
		return true;
	}
	else
	{
		return false;
	}
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
	DWORD read;
	BOOL b=ReadFile(hfile, buffer, bsize, &read, NULL );
	if(b==FALSE)
	{
		if(has_error)
		{
			*has_error=true;
		}
	}
	return (_u32)read;
}

_u32 File::Read(int64 spos, char* buffer, _u32 bsize, bool *has_error)
{
	OVERLAPPED overlapped = {};
	LARGE_INTEGER li;
	li.QuadPart = spos;
	overlapped.Offset = li.LowPart;
	overlapped.OffsetHigh = li.HighPart;

	DWORD read;
	BOOL b=ReadFile(hfile, buffer, bsize, &read, &overlapped );
	if(b==FALSE)
	{
		if (has_error)
		{
			*has_error = true;
		}
	}
	return (_u32)read;
}

_u32 File::Write(const std::string &tw, bool *has_error)
{
	return Write( tw.c_str(), (_u32)tw.size(), has_error );
}

_u32 File::Write(int64 spos, const std::string &tw, bool *has_error)
{
	return Write(spos, tw.c_str(), (_u32)tw.size(), has_error);
}

_u32 File::Write(const char* buffer, _u32 bsize, bool *has_error)
{
	DWORD written;
	if (WriteFile(hfile, buffer, bsize, &written, NULL) == FALSE)
	{
		if (has_error)
		{
			*has_error = true;
		}
	}
	return written;
}

_u32 File::Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error)
{
	OVERLAPPED overlapped = {};
	LARGE_INTEGER li;
	li.QuadPart = spos;
	overlapped.Offset = li.LowPart;
	overlapped.OffsetHigh = li.HighPart;

	DWORD written;
	if (WriteFile(hfile, buffer, bsize, &written, &overlapped) == FALSE)
	{
		if (has_error)
		{
			*has_error = true;
		}
	}
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
	if( hfile!=INVALID_HANDLE_VALUE )
	{
		BOOL b=CloseHandle( hfile );
		hfile=INVALID_HANDLE_VALUE;
	}
}

File::os_file_handle File::getOsHandle(bool release_handle)
{
	HANDLE ret = hfile;
	if (release_handle)
	{
		hfile = INVALID_HANDLE_VALUE;
	}
	return ret;
}

bool File::setSparse()
{
	if (!is_sparse)
	{
		FILE_SET_SPARSE_BUFFER buf = { TRUE };
		DWORD ret_bytes;
		BOOL b = DeviceIoControl(hfile, FSCTL_SET_SPARSE, &buf,
			static_cast<DWORD>(sizeof(buf)), NULL, 0, &ret_bytes, NULL);

		if (!b)
		{
			return false;
		}

		is_sparse = true;
	}

	return true;
}

bool File::PunchHole( _i64 spos, _i64 size )
{
	if (!setSparse())
	{
		return false;
	}
	
	FILE_ZERO_DATA_INFORMATION zdi;

	zdi.FileOffset.QuadPart = spos;
	zdi.BeyondFinalZero.QuadPart = spos + size;

	DWORD ret_bytes;
	BOOL b = DeviceIoControl(hfile, FSCTL_SET_ZERO_DATA, &zdi,
		static_cast<DWORD>(sizeof(zdi)), NULL, 0, &ret_bytes, 0);

	if(!b)
	{
		return false;
	}
	else
	{
		return true;
	}
}

bool File::Sync()
{
	return FlushFileBuffers(hfile)!=0;
}

bool File::Resize(int64 new_size, bool set_sparse)
{
	int64 fsize = Size();

	if (new_size > fsize
		&& set_sparse)
	{
		if (!setSparse())
		{
			return false;
		}
	}

	LARGE_INTEGER tmp;
	tmp.QuadPart = 0;
	LARGE_INTEGER curr_pos;
	if (SetFilePointerEx(hfile, tmp, &curr_pos, FILE_CURRENT) == FALSE)
	{
		return false;
	}

	tmp.QuadPart = new_size;
	if (SetFilePointerEx(hfile, tmp, NULL, FILE_BEGIN) == FALSE)
	{
		return false;
	}

	BOOL ret = SetEndOfFile(hfile);

	SetFilePointerEx(hfile, curr_pos, NULL, FILE_BEGIN);

	return ret == TRUE;
}

void File::resetSparseExtentIter()
{
	res_extent_buffer.clear();
	more_extents = true;
	curr_extent = 0;
}

File::SSparseExtent File::nextSparseExtent()
{
	while (!res_extent_buffer.empty()
		&& curr_extent<res_extent_buffer.size())
	{
		if (res_extent_buffer[curr_extent].FileOffset.QuadPart != last_sparse_pos)
		{
			File::SSparseExtent ret(last_sparse_pos, res_extent_buffer[curr_extent].FileOffset.QuadPart - last_sparse_pos);
			last_sparse_pos = res_extent_buffer[curr_extent].FileOffset.QuadPart + res_extent_buffer[curr_extent].Length.QuadPart;
			++curr_extent;
			return ret;
		}
		
		last_sparse_pos = res_extent_buffer[curr_extent].FileOffset.QuadPart + res_extent_buffer[curr_extent].Length.QuadPart;
		++curr_extent;
	}

	if (!more_extents)
	{
		int64 fsize = Size();
		if (last_sparse_pos!=-1 && last_sparse_pos != fsize)
		{
			File::SSparseExtent ret = File::SSparseExtent(last_sparse_pos, fsize - last_sparse_pos);
			last_sparse_pos = fsize;
			return ret;
		}

		return File::SSparseExtent();
	}

	int64 fsize = Size();

	FILE_ALLOCATED_RANGE_BUFFER query_range;
	query_range.FileOffset.QuadPart = last_sparse_pos;
	query_range.Length.QuadPart = fsize- last_sparse_pos;
	
	if (res_extent_buffer.empty())
	{
		res_extent_buffer.resize(10);
	}
	else
	{
		res_extent_buffer.resize(100);
	}

	DWORD output_bytes;
	BOOL b = DeviceIoControl(hfile, FSCTL_QUERY_ALLOCATED_RANGES,
		&query_range, sizeof(query_range), res_extent_buffer.data(), static_cast<DWORD>(res_extent_buffer.size()*sizeof(FILE_ALLOCATED_RANGE_BUFFER)),
		&output_bytes, NULL);

	more_extents = (!b && GetLastError() == ERROR_MORE_DATA);

	if (more_extents || b)
	{
		res_extent_buffer.resize(output_bytes / sizeof(FILE_ALLOCATED_RANGE_BUFFER));
		curr_extent = 0;
	}
	else
	{
		res_extent_buffer.clear();
		more_extents = false;
		curr_extent = 0;
		last_sparse_pos = -1;
	}

	return nextSparseExtent();
}

std::vector<File::SFileExtent> File::getFileExtents(int64 starting_offset, int64 block_size, bool& more_data, unsigned int flags)
{
	std::vector<File::SFileExtent> ret;

	STARTING_VCN_INPUT_BUFFER starting_vcn;
	starting_vcn.StartingVcn.QuadPart = starting_offset / block_size;

	std::vector<char> buf;
	buf.resize(4096);

	DWORD retBytes;
	BOOL b = DeviceIoControl(hfile, FSCTL_GET_RETRIEVAL_POINTERS,
		&starting_vcn, sizeof(starting_vcn),
		buf.data(), buf.size(), &retBytes, NULL);

	more_data = (!b && GetLastError() == ERROR_MORE_DATA);

	if (more_data || b)
	{
		PRETRIEVAL_POINTERS_BUFFER pbuf = reinterpret_cast<PRETRIEVAL_POINTERS_BUFFER>(buf.data());
		LARGE_INTEGER last_vcn = pbuf->StartingVcn;
		for (DWORD i = 0; i < pbuf->ExtentCount; ++i)
		{
			if (pbuf->Extents[i].Lcn.QuadPart != -1)
			{
				int64 count = pbuf->Extents[i].NextVcn.QuadPart - last_vcn.QuadPart;

				File::SFileExtent ext;
				ext.offset = last_vcn.QuadPart * block_size;
				ext.size = count * block_size;
				ext.volume_offset = pbuf->Extents[i].Lcn.QuadPart * block_size;

				ret.push_back(ext);
			}

			last_vcn = pbuf->Extents[i].NextVcn;
		}
	}

	return ret;
}

#endif
