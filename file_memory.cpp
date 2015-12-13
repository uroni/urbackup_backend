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

#include "file_memory.h"

#ifndef _WIN32
#include <memory.h>
#endif

CMemoryFile::CMemoryFile()
{
	pos=0;
}

std::string CMemoryFile::Read(_u32 tr)
{
	if(pos>=data.size() )
		return "";

	size_t rtr=(std::min)((size_t)tr, data.size()-pos);
	std::string ret;
	ret.resize(rtr);
	memcpy(&ret[0], &data[pos], rtr);
	pos+=rtr;

	return ret;
}

_u32 CMemoryFile::Read(char* buffer, _u32 bsize)
{
	if(pos>=data.size() )
		return 0;

	size_t rtr=(std::min)((size_t)bsize, data.size()-pos);
	memcpy(buffer, &data[pos], rtr);
	pos+=rtr;

	return (_u32)rtr;
}

_u32 CMemoryFile::Write(const std::string &tw)
{
	return Write(tw.c_str(), (_u32)tw.size());
}

_u32 CMemoryFile::Write(const char* buffer, _u32 bsize)
{
	if(pos+bsize>data.size())
	{
		data.resize(pos+bsize);
		memcpy(&data[pos], buffer, bsize);
		pos+=bsize;
		return bsize;
	}
	else
	{
		memcpy(&data[pos], buffer, bsize);
		pos+=bsize;
		return bsize;
	}
}
bool CMemoryFile::Seek(_i64 spos)
{
	if((size_t)spos<data.size() && spos>=0)
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
	return data.size();
}

_i64 CMemoryFile::RealSize()
{
	return Size();
}

std::string CMemoryFile::getFilename()
{
	return "_MEMORY_";
}

bool CMemoryFile::PunchHole( _i64 spos, _i64 size )
{
	return false;
}

bool CMemoryFile::Sync()
{
	return false;
}
