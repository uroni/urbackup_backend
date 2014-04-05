/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "filesystem.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include <memory.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#endif

Filesystem::Filesystem(const std::wstring &pDev)
{
	has_error=false;

	dev=Server->openFile(pDev, MODE_READ_DEVICE);
	if(dev==NULL)
	{
		int last_error;
#ifdef _WIN32
		last_error=GetLastError();
#else
		last_error=errno;
#endif
		Server->Log("Error opening device file. Errorcode: "+nconvert(last_error), LL_ERROR);
		has_error=true;
	}
	tmp_buf=NULL;
	own_dev=true;
}

Filesystem::Filesystem(IFile *pDev)
	: dev(pDev)
{
	has_error=false;
	tmp_buf=NULL;
	own_dev=false;
}

Filesystem::~Filesystem()
{
	if(dev!=NULL && own_dev)
	{
		Server->destroy(dev);
	}
	if(tmp_buf!=NULL)
		delete [] tmp_buf;
}

bool Filesystem::readBlock(int64 pBlock, char * buffer)
{
	const unsigned char *bitmap=getBitmap();
	int64 blocksize=getBlocksize();

	size_t bitmap_byte=(size_t)(pBlock/8);
	size_t bitmap_bit=pBlock%8;

	unsigned char b=bitmap[bitmap_byte];

	bool has_bit=((b & (1<<bitmap_bit))>0);

	if(!has_bit)
		return false;
	else if(buffer!=NULL)
	{
		bool b=dev->Seek(pBlock*blocksize);
		if(!b)
		{
			Server->Log("Seeking in device failed -1", LL_ERROR);
			has_error=true;
			return false;
		}
		if(!readFromDev(buffer, (_u32)blocksize) )
		{
			Server->Log("Reading from device failed -1", LL_ERROR);
			has_error=true;
			return false;
		}
		return true;
	}
	else
		return true;
}

std::vector<int64> Filesystem::readBlocks(int64 pStartBlock, unsigned int n, const std::vector<char*> buffers, unsigned int buffer_offset)
{
	const unsigned char *bitmap=getBitmap();
	_u32 blocksize=(_u32)getBlocksize();
	std::vector<int64> ret;

	unsigned int currbuf=0;

	bool has_all=true;
	for(int64 i=pStartBlock;i<pStartBlock+n;++i)
	{
		size_t bitmap_byte=(size_t)(i/8);
		size_t bitmap_bit=i%8;

		unsigned char b=bitmap[bitmap_byte];

		bool has_bit=((b & (1<<bitmap_bit))>0);

		if(!has_bit)
		{
			has_all=false;
			break;
		}
	}

	if(has_all)
	{
		if(tmp_buf==NULL || tmpbufsize!=n*blocksize )
		{
			if(tmp_buf!=NULL) delete [] tmp_buf;
			tmpbufsize=n*blocksize;
			tmp_buf=new char[tmpbufsize];
		}

		if(!dev->Seek(pStartBlock*blocksize))
		{
			Server->Log("Seeking in device failed -2", LL_ERROR);
			has_error=true;
			return std::vector<int64>();
		}
		if(!readFromDev(tmp_buf, n*(_u32)blocksize))
		{
			Server->Log("Reading from device failed -2", LL_ERROR);
			has_error=true;
			return std::vector<int64>();
		}
		for(int64 i=pStartBlock;i<pStartBlock+n;++i)
		{
			memcpy(buffers[currbuf]+buffer_offset, tmp_buf+currbuf*blocksize, blocksize);
			++currbuf;
			ret.push_back(i);
		}
	}
	else
	{
		for(int64 i=pStartBlock;i<pStartBlock+n;++i)
		{
			if(readBlock(i, buffers[currbuf]+buffer_offset) )
			{
				++currbuf;
				ret.push_back(i);
			}
		}
	}

	return ret;
}

bool Filesystem::readFromDev(char *buf, _u32 bsize)
{
	int tries=20;
	_u32 rc=dev->Read(buf, bsize);
	while(rc<bsize)
	{
		Server->wait(200);
		Server->Log("Reading from device failed. Retrying.", LL_WARNING);
		rc+=dev->Read(buf+rc, bsize-rc);
		--tries;
		if(tries<0)
		{
			Server->Log("Reading from device failed.", LL_ERROR);
			return false;
		}
	}
	return true;
}

int64 Filesystem::calculateUsedSpace(void)
{
	const unsigned char *bm=getBitmap();
	uint64 blocks1=getSize()/getBlocksize();
	unsigned int tadd=(unsigned int)(blocks1/8);;
	if( blocks1%8>0)
		++tadd;

	const unsigned char *target=bm+tadd;
	int64 used_blocks=0;
	uint64 blocknum=0;
	while(bm!=target)
	{
		const unsigned char b=*bm;
		for(int i=0;i<8;++i)
		{
			if(blocknum>=blocks1)
				break;
			if( (b & (1<<i))>0 )
			{
				++used_blocks;
			}
			++blocknum;
		}
		++bm;
	}
	return used_blocks*getBlocksize();
}

bool Filesystem::hasError(void)
{
	return has_error;
}