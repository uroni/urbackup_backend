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

#include "unknown.h"
#include "../../Interface/Server.h"
#include <memory.h>

#ifdef _WIN32
#include <Windows.h>
#endif

#define DEF_BLOCKSIZE 4096

FSUnknown::FSUnknown(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback)
	: Filesystem(pDev, read_ahead, next_block_callback), bitmap(NULL)
{
	if(has_error)
		return;

#ifdef _WIN32
	HANDLE hDev=CreateFileW( Server->ConvertToWchar(pDev).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_NO_BUFFERING, NULL );
	if(hDev==INVALID_HANDLE_VALUE)
	{
		Server->Log("Error opening device -2", LL_ERROR);
		has_error=true;
		return;
	}

	DWORD r_bytes;
	GET_LENGTH_INFORMATION li;
	BOOL b=DeviceIoControl(hDev, IOCTL_DISK_GET_LENGTH_INFO,  NULL,  0, &li,  sizeof(GET_LENGTH_INFORMATION), &r_bytes, NULL);
	CloseHandle(hDev);
	if(!b)
	{
		Server->Log("Error in DeviceIoControl(IOCTL_DISK_GET_LENGTH_INFO)", LL_ERROR);
		has_error=true;
		return;
	}
	drivesize=li.Length.QuadPart;
#else
	drivesize=dev->Size();
#endif

	blocksize = DEF_BLOCKSIZE;

	if (drivesize%blocksize != 0)
	{
		Server->Log("Drive size not aligned to 4096 bytes. Using 512 bytes.", LL_INFO);
		blocksize = 512;
	}

	int64 bitmap_entries=(int64)(drivesize/ blocksize);
	if(drivesize%blocksize !=0)
		++bitmap_entries;

	size_t bitmap_bytes=(size_t)(bitmap_entries/8);

	if(bitmap_entries%8!=0)
		++bitmap_bytes;

	bitmap=new unsigned char[bitmap_bytes];
	memset(bitmap, 0xFF, bitmap_bytes);

	initReadahead(read_ahead, background_priority);
}

FSUnknown::~FSUnknown(void)
{
	delete []bitmap;
}

int64 FSUnknown::getBlocksize(void)
{
	return blocksize;
}

int64 FSUnknown::getSize(void)
{
	return drivesize;
}

const unsigned char *FSUnknown::getBitmap(void)
{
	return bitmap;
}