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

#include "unknown.h"
#include <memory.h>

#define DEF_BLOCKSIZE 4096

FSUnknown::FSUnknown(const std::wstring &pDev) : Filesystem(pDev)
{
	if(has_error)
		return;

	int64 bitmap_entries=(int64)(dev->Size()/DEF_BLOCKSIZE);
	if(dev->Size()%DEF_BLOCKSIZE!=0)
		++bitmap_entries;

	size_t bitmap_bytes=(size_t)(bitmap_entries/8);

	if(bitmap_entries%8!=0)
		++bitmap_bytes;

	bitmap=new unsigned char[bitmap_bytes];
	memset(bitmap, 0xFF, bitmap_bytes);
}

FSUnknown::~FSUnknown(void)
{
	delete []bitmap;
}

int64 FSUnknown::getBlocksize(void)
{
	return DEF_BLOCKSIZE;
}

int64 FSUnknown::getSize(void)
{
	return dev->Size();
}

const unsigned char *FSUnknown::getBitmap(void)
{
	return bitmap;
}