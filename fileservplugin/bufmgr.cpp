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

namespace fileserv
{

CBufMgr::CBufMgr(unsigned int nbuf, unsigned int bsize)
{
	for(unsigned int i=0;i<nbuf;++i)
	{
		SBuffer buf;
		buf.buffer=new char[bsize+1];
		buf.used=false;
		buffers.push_back( buf );
	}
	freebufs=nbuf;
}

CBufMgr::~CBufMgr(void)
{
	for(size_t i=0;i<buffers.size();++i)
	{
		if( buffers[i].used==true )
		{
			Log("Warning: Deleting used Buffer!");
		}
		delete[] buffers[i].buffer;
	}
}

char* CBufMgr::getBuffer(void)
{
	for(size_t i=0;i<buffers.size();++i)
	{
		if( buffers[i].used==false )
		{
			buffers[i].used=true;
			--freebufs;
			return buffers[i].buffer;
		}
	}
	return NULL;
}

void CBufMgr::releaseBuffer(char* buf)
{
	for(size_t i=0;i<buffers.size();++i)
	{
		if( buffers[i].buffer==buf )
		{
			++freebufs;
			buffers[i].used=false;
			return;
		}		
	}
	Log("Warning: Buffer to free not found!");
}

unsigned int CBufMgr::nfreeBufffer(void)
{
	return freebufs;
}

} //namespace fileserv