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

#include "bufmgr.h"
#include "../Interface/Server.h"
#include "os_functions.h"

CBufMgr::CBufMgr(unsigned int nbuf, unsigned int bsize)
{
	for(unsigned int i=0;i<nbuf;++i)
	{
		SBuffer buf;
		buf.buffer=new char[bsize];
		buf.used=false;
		buffers.push_back( buf );
	}
	freebufs=nbuf;

	mutex=Server->createMutex();
	cond=Server->createCondition();
}

CBufMgr::~CBufMgr(void)
{
	for(size_t i=0;i<buffers.size();++i)
	{
		if( buffers[i].used==true )
		{
			Server->Log("Warning: Deleting used Buffer!", LL_DEBUG);
		}
		delete[] buffers[i].buffer;
	}
	Server->destroy(mutex);
	Server->destroy(cond);
}

char* CBufMgr::getBuffer(void)
{
	IScopedLock lock(mutex);
	while(true)
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
		Server->Log("Buffers full... -1", LL_INFO);
		cond->wait(&lock);
	}
}

std::vector<char*> CBufMgr::getBuffers(unsigned int n)
{
	std::vector<char*> ret;
	if(n==0)
		return ret;
	IScopedLock lock(mutex);
	while(true)
	{
		for(size_t i=0;i<buffers.size();++i)
		{
			if( buffers[i].used==false )
			{
				buffers[i].used=true;
				--freebufs;
				ret.push_back(buffers[i].buffer);
				if(ret.size()>=n)
					return ret;
			}
		}
		Server->Log("Buffers full... -2", LL_INFO);
		cond->wait(&lock);
	}
}

void CBufMgr::releaseBuffer(char* buf)
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<buffers.size();++i)
	{
		if( buffers[i].buffer==buf )
		{
			++freebufs;
			buffers[i].used=false;
			cond->notify_one();
			return;
		}		
	}
	Server->Log("Warning: Buffer to free not found!", LL_WARNING);
}

unsigned int CBufMgr::nfreeBufffer(void)
{
	IScopedLock lock(mutex);
	return freebufs;
}

CBufMgr2::CBufMgr2(unsigned int nbuf, unsigned int bsize)
{
	bufptr=new char[nbuf*bsize];
	for(unsigned int i=0;i<nbuf;++i)
	{
		free_bufs.push(bufptr+i*bsize);
	}

	mutex=Server->createMutex();
	cond=Server->createCondition();
}

CBufMgr2::~CBufMgr2(void)
{
	delete [] bufptr;
	Server->destroy(mutex);
	Server->destroy(cond);
}

char* CBufMgr2::getBuffer(void)
{
	IScopedLock lock(mutex);
	while(free_bufs.empty())
		cond->wait(&lock);
	char *ret=free_bufs.top();
	free_bufs.pop();
	return ret;
}

std::vector<char*> CBufMgr2::getBuffers(unsigned int n)
{
	std::vector<char*> ret;
	IScopedLock lock(mutex);
	while(ret.size()<n)
	{
		while(free_bufs.empty())
			cond->wait(&lock);
		ret.push_back(free_bufs.top());
		free_bufs.pop();
	}
	return ret;
}

void CBufMgr2::releaseBuffer(char* buf)
{
	IScopedLock lock(mutex);
	free_bufs.push(buf);
	cond->notify_one();
}

unsigned int CBufMgr2::nfreeBufffer(void)
{
	IScopedLock lock(mutex);
	return (_u32)free_bufs.size();
}

CFileBufMgr::CFileBufMgr(bool pMemory)
{
	memory=pMemory;
}

CFileBufMgr::~CFileBufMgr(void)
{
}

IFile* CFileBufMgr::getBuffer(void)
{
	return openFileRetry();
}

std::vector<IFile*> CFileBufMgr::getBuffers(unsigned int n)
{
	std::vector<IFile*> ret;
	while(ret.size()<n)
	{
		ret.push_back(openFileRetry());
	}
	return ret;
}

void CFileBufMgr::releaseBuffer(IFile *buf)
{
	std::wstring fn=buf->getFilenameW();
	Server->destroy(buf);
	if(!Server->deleteFile(fn))
	{
		Server->Log("Deleting buffer file failed. Truncating it...", LL_ERROR);
		os_file_truncate(fn, 0);
		Server->deleteFile(fn);
	}
}

IFile* CFileBufMgr::openFileRetry(void)
{
	if(!memory)
	{
		IFile *r=NULL;
		while(r==NULL)
		{
			r=Server->openTemporaryFile();
			if(r==NULL)
				Server->wait(10000);
		}
		return r;
	}
	else
		return Server->openMemoryFile();
}