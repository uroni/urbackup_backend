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

#include "../vld.h"
#include "bufmgr.h"
#include "log.h"
#ifdef _WIN32
#include <Windows.h>
#endif

namespace fileserv
{

CBufMgr::CBufMgr(unsigned int nbuf, unsigned int bsize)
{
	for(unsigned int i=0;i<nbuf;++i)
	{
		char* buffer;
#ifndef _WIN32
		buffer=new char[bsize];
#else
		buffer = reinterpret_cast<char*>(VirtualAlloc(NULL, bsize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#endif
		buffers.push_back( buffer );
		free_buffers.push(buffer);
	}
}

CBufMgr::~CBufMgr(void)
{
	if (free_buffers.size() != buffers.size())
	{
		Log("There are used buffers when freeing buffer manager", LL_ERROR);
	}

	for(size_t i=0;i<buffers.size();++i)
	{
#ifndef _WIN32
		delete[] buffers[i];
#else
		VirtualFree(buffers[i], 0, MEM_RELEASE);
#endif
	}
}

char* CBufMgr::getBuffer(void)
{
	if (!free_buffers.empty())
	{
		char* buffer = free_buffers.top();
		free_buffers.pop();
		return buffer;
	}
	else
	{
		return NULL;
	}
}

void CBufMgr::releaseBuffer(char* buf)
{
	free_buffers.push(buf);
}

unsigned int CBufMgr::nfreeBufffer(void)
{
	return static_cast<unsigned int>(free_buffers.size());
}

} //namespace fileserv