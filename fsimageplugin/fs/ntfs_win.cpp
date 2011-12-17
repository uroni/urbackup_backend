#include "ntfs_win.h"
#include <Windows.h>
#include "../../Interface/Server.h"
#include "../../stringtools.h"

FSNTFSWIN::FSNTFSWIN(const std::wstring &pDev)
	: Filesystem(pDev), bitmap(NULL)
{
	HANDLE hDev=CreateFileW( pDev.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if(hDev==INVALID_HANDLE_VALUE)
	{
		Server->Log("Error opening device -2", LL_ERROR);
		has_error=true;
		return;
	}

	DWORD sectors_per_cluster;
	DWORD bytes_per_sector;
	DWORD NumberOfFreeClusters;
	DWORD TotalNumberOfClusters;
	BOOL b=GetDiskFreeSpaceW((pDev+L"\\").c_str(), &sectors_per_cluster, &bytes_per_sector, &NumberOfFreeClusters, &TotalNumberOfClusters);
	if(!b)
	{
		Server->Log("Error in GetDiskFreeSpaceW", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	sectorsize=bytes_per_sector;
	clustersize=sectorsize*sectors_per_cluster;
	drivesize=(uint64)TotalNumberOfClusters*(uint64)clustersize;

	STARTING_LCN_INPUT_BUFFER start;
	LARGE_INTEGER li;
	li.QuadPart=0;
	start.StartingLcn=li;

	size_t new_size=TotalNumberOfClusters/8;
	if(TotalNumberOfClusters%8!=0)
		++new_size;
			
	new_size+=sizeof(VOLUME_BITMAP_BUFFER);

	VOLUME_BITMAP_BUFFER *vbb=(VOLUME_BITMAP_BUFFER*)(new char[new_size]);

	DWORD r_bytes;
	b=DeviceIoControl(hDev, FSCTL_GET_VOLUME_BITMAP, &start, sizeof(STARTING_LCN_INPUT_BUFFER), vbb, (DWORD)new_size, &r_bytes, NULL);
	if(!b)
	{
		Server->Log("DeviceIoControl(1) failed", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	bitmap=new unsigned char[(unsigned int)(vbb->BitmapSize.QuadPart/8)];
	memcpy(bitmap, vbb->Buffer, (size_t)(vbb->BitmapSize.QuadPart/8));

	delete []vbb;

	CloseHandle(hDev);
}

FSNTFSWIN::~FSNTFSWIN(void)
{
	delete [] bitmap;
}

int64 FSNTFSWIN::getBlocksize(void)
{
	return clustersize;
}

int64 FSNTFSWIN::getSize(void)
{
	return drivesize;
}

const unsigned char * FSNTFSWIN::getBitmap(void)
{
	return bitmap;
}