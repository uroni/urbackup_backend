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

#include "filesystem.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include <memory.h>

Filesystem::Filesystem(const std::wstring &pDev)
{
	dev=Server->openFile(pDev, MODE_READ);
	if(dev==NULL)
		Server->Log("Error opening device file", LL_ERROR);
	tmp_buf=NULL;
}

Filesystem::~Filesystem()
{
	if(dev!=NULL)
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
		dev->Seek(pBlock*blocksize);
		dev->Read(buffer, (_u32)blocksize);
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

		dev->Seek(pStartBlock*blocksize);
		dev->Read(tmp_buf, n*(_u32)blocksize);
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