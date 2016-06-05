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

#include "vld.h"
#include "file.h"
#include "types.h"
#include "stringtools.h"

#ifdef MODE_STL

File::File()
{

}

bool File::Open(std::string pfn, int mode)
{
	if (mode == MODE_RW_RESTORE)
	{
		mode = MODE_RW;
	}
	if (mode == MODE_RW_CREATE_RESTORE)
	{
		mode = MODE_RW_CREATE;
	}

	fn=pfn;
	std::ios::openmode _mode;
	if( mode==MODE_READ || mode==MODE_READ_DEVICE || mode==MODE_READ_SEQUENTIAL || mode==MODE_READ_SEQUENTIAL_BACKUP)
		_mode=std::ios::in|std::ios::binary;
	else if( mode==MODE_WRITE || mode==MODE_TEMP )
	{
		_unlink(pfn.c_str());
		_mode=std::ios::out|std::ios::binary;
	}
	else if( mode==MODE_APPEND )
		_mode=std::ios::app|std::ios::binary;
	else if( mode==MODE_RW )
		_mode=std::ios::in|std::ios::out|std::ios::binary;

	fi.open(wnarrow(fn).c_str(), _mode);
	
	if( fi.is_open()==true )
		return true;
	else
		return false;
}

bool File::Open(void *handle, const std::string& pFilename)
{
	//Not supported
	return false;
}

bool File::OpenTemporaryFile(const std::string &dir, bool first_try)
{
	return Open(tmpnam(NULL), MODE_TEMP);
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
	fi.read( buffer, bsize);
	_u32 read=fi.gcount();
	return read;
}

_u32 File::Write(const std::string &tw)
{
	return Write( tw.c_str(), (_u32)tw.size() );
}

_u32 File::Write(const char* buffer, _u32 bsize)
{
	fi.write( buffer, bsize);
	return bsize;
}

bool File::Seek(_i64 spos)
{
	fi.seekg((std::ios::off_type)spos, std::ios::beg);
	fi.seekp((std::ios::off_type)spos, std::ios::beg);
	return true;
}

_i64 File::Size(void)
{
	std::ios::pos_type cp=fi.tellg();
	fi.seekg(0, std::ios::end);
	_i64 fsize=fi.tellg();
	fi.seekg(cp, std::ios::beg);
	return fsize;	
}

_i64 File::RealSize(void)
{
	return Size();
}

void File::Close()
{
	fi.close();
}

bool File::PunchHole( _i64 spos, _i64 size )
{
	return false;
}

bool File::Sync()
{
	return false;
}

#endif
