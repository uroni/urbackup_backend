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

#include "ntfs_win.h"
#include <Windows.h>
#include <algorithm>
#include "../../Interface/Server.h"
#include "../../stringtools.h"
#include "../../urbackupcommon/os_functions.h"

FSNTFSWIN::FSNTFSWIN(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback)
	: Filesystem(pDev, read_ahead, next_block_callback), bitmap(NULL)
{
	HANDLE hDev=CreateFileW( Server->ConvertToWchar(pDev).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_NO_BUFFERING, NULL );
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
	BOOL b=GetDiskFreeSpaceW((Server->ConvertToWchar(pDev)+L"\\").c_str(), &sectors_per_cluster, &bytes_per_sector, &NumberOfFreeClusters, &TotalNumberOfClusters);
	if(!b)
	{
		Server->Log("Error in GetDiskFreeSpaceW", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	DWORD r_bytes;
	NTFS_VOLUME_DATA_BUFFER vdb;
	b=DeviceIoControl(hDev, FSCTL_GET_NTFS_VOLUME_DATA,  NULL,  0, &vdb,  sizeof(NTFS_VOLUME_DATA_BUFFER), &r_bytes, NULL);
	if(!b)
	{
		Server->Log("Error in DeviceIoControl(FSCTL_GET_NTFS_VOLUME_DATA)", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	sectorsize=bytes_per_sector;
	clustersize=sectorsize*sectors_per_cluster;
	drivesize=vdb.NumberSectors.QuadPart*sectorsize;

	uint64 numberOfClusters=drivesize/clustersize;

	STARTING_LCN_INPUT_BUFFER start;
	LARGE_INTEGER li;
	li.QuadPart=0;
	start.StartingLcn=li;

	size_t new_size=(size_t)(numberOfClusters/8);
	if(numberOfClusters%8!=0)
		++new_size;
			
	size_t n_clusters=new_size;

	new_size+=sizeof(VOLUME_BITMAP_BUFFER);

	VOLUME_BITMAP_BUFFER *vbb=(VOLUME_BITMAP_BUFFER*)(new char[new_size]);

	
	b=DeviceIoControl(hDev, FSCTL_GET_VOLUME_BITMAP, &start, sizeof(STARTING_LCN_INPUT_BUFFER), vbb, (DWORD)new_size, &r_bytes, NULL);
	if(!b)
	{
		Server->Log("DeviceIoControl(FSCTL_GET_VOLUME_BITMAP) failed", LL_ERROR);
		has_error=true;
		CloseHandle(hDev);
		return;
	}

	Server->Log("TotalNumberOfClusters="+convert((size_t)TotalNumberOfClusters)+" numberOfClusters="+convert(numberOfClusters)+" n_clusters="+convert(n_clusters)+" StartingLcn="+convert(vbb->StartingLcn.QuadPart)+" BitmapSize="+convert(vbb->BitmapSize.QuadPart)+" r_bytes="+convert((size_t)r_bytes), LL_DEBUG);

	bitmap=new unsigned char[(unsigned int)(n_clusters)];
	memset(bitmap, 0xFF, n_clusters);
	memcpy(bitmap+(unsigned int)(vbb->StartingLcn.QuadPart/8), vbb->Buffer, (size_t)(vbb->BitmapSize.QuadPart/8));

	delete []vbb;

	CloseHandle(hDev);

	initReadahead(read_ahead, background_priority);
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

void FSNTFSWIN::logFileChanges(std::string volpath, int64 min_size, char* fc_bitmap)
{
	if (!volpath.empty()
		&& volpath[volpath.size() - 1] == '\\')
	{
		volpath.erase(volpath.size() - 1, 1);
	}

	int64 total_files = 0;
	int64 total_changed_sectors = 0;
	logFileChangesInt(volpath, min_size, fc_bitmap, total_files, total_changed_sectors);

	Server->Log("Total: " + convert(total_changed_sectors) + " changed sectors (" + PrettyPrintBytes(total_changed_sectors*getBlocksize()) + ") in " + convert(total_files) + " files");
}

void FSNTFSWIN::logFileBlocks(std::string volpath, const std::vector<int64>& blocks)
{
	std::vector<SFile> files = getFiles(volpath);

	for (size_t i = 0; i < files.size(); ++i)
	{
		if (files[i].isdir)
		{
			if (!files[i].issym)
			{
				logFileBlocks(volpath + os_file_sep() + files[i].name, blocks);
			}
		}
		else if (!files[i].issym)
		{
			HANDLE hFile = CreateFileW(Server->ConvertToWchar(os_file_prefix(volpath + os_file_sep() + files[i].name)).c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if (hFile != INVALID_HANDLE_VALUE)
			{
				STARTING_VCN_INPUT_BUFFER start_vcn = {};
				std::vector<char> ret_buf;
				ret_buf.resize(32768);

				int64 block_start = -1;
				int64 block_end = -1;

				while (true)
				{
					RETRIEVAL_POINTERS_BUFFER* ret_ptrs = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER*>(ret_buf.data());

					DWORD bytesRet = 0;

					BOOL b = DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS,
						&start_vcn, sizeof(start_vcn), ret_ptrs, static_cast<DWORD>(ret_buf.size()), &bytesRet, NULL);

					DWORD err = GetLastError();

					if (b || err == ERROR_MORE_DATA)
					{
						LARGE_INTEGER last_vcn = ret_ptrs->StartingVcn;
						for (DWORD i = 0; i<ret_ptrs->ExtentCount; ++i)
						{
							if (ret_ptrs->Extents[i].Lcn.QuadPart != -1)
							{
								int64 count = ret_ptrs->Extents[i].NextVcn.QuadPart - last_vcn.QuadPart;
								int64 dest = ret_ptrs->Extents[i].Lcn.QuadPart + count;
								for (int64 pos = ret_ptrs->Extents[i].Lcn.QuadPart;
										pos < dest; ++pos)
								{
									bool found = std::binary_search(blocks.begin(), blocks.end(), pos);

									if (!found)
									{
										if (block_start != -1)
										{
											Server->Log("Damaged block found in file \"" + volpath + os_file_sep() + files[i].name + "\" at position " + convert(block_start)+" length "+PrettyPrintBytes((block_end-block_start)*getBlocksize()), LL_INFO);
											block_start = -1;
										}
									}
									else
									{
										if (block_start == -1)
										{
											block_start = last_vcn.QuadPart + (pos - ret_ptrs->Extents[i].Lcn.QuadPart);
											block_end = block_start + 1;
										}
										else
										{
											block_end = last_vcn.QuadPart + (pos - ret_ptrs->Extents[i].Lcn.QuadPart) + 1;
										}
									}
								}
							}

							last_vcn = ret_ptrs->Extents[i].NextVcn;
						}
					}

					if (!b)
					{
						if (err == ERROR_MORE_DATA)
						{
							start_vcn.StartingVcn = ret_ptrs->Extents[ret_ptrs->ExtentCount - 1].NextVcn;
						}
						else if (err == ERROR_HANDLE_EOF)
						{
							CloseHandle(hFile);
							break;
						}
						else
						{
							Server->Log("Error " + convert((int)GetLastError()) + " while accessing retrieval points", LL_WARNING);
							CloseHandle(hFile);
							break;
						}
					}
					else
					{
						CloseHandle(hFile);
						break;
					}
				}

				if (block_start != -1)
				{
					Server->Log("Damaged block found in file \"" + volpath + os_file_sep() + files[i].name + "\" at position " + convert(block_start) + " length " + PrettyPrintBytes((block_end - block_start)*getBlocksize()), LL_INFO);
					block_start = -1;
				}
			}
			else
			{
				//Server->Log("Error opening file: " + volpath + os_file_sep() + files[i].name, LL_INFO);
			}
		}
	}
}

void FSNTFSWIN::logFileChangesInt(const std::string& volpath, int64 min_size, char* fc_bitmap, int64& total_files, int64& total_changed_sectors)
{
	std::vector<SFile> files = getFiles(volpath);

	for (size_t i = 0; i < files.size(); ++i)
	{
		if (files[i].isdir)
		{
			if (!files[i].issym)
			{
				logFileChangesInt(volpath + os_file_sep() + files[i].name, min_size, fc_bitmap, total_files, total_changed_sectors);
			}
		}
		else if(!files[i].issym)
		{
			HANDLE hFile = CreateFileW(Server->ConvertToWchar(os_file_prefix(volpath + os_file_sep() + files[i].name)).c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
				OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

			if (hFile != INVALID_HANDLE_VALUE)
			{
				int64 sector_count = 0;
				int64 total_sectors = 0;

				STARTING_VCN_INPUT_BUFFER start_vcn = {};
				std::vector<char> ret_buf;
				ret_buf.resize(32768);

				while (true)
				{
					RETRIEVAL_POINTERS_BUFFER* ret_ptrs = reinterpret_cast<RETRIEVAL_POINTERS_BUFFER*>(ret_buf.data());

					DWORD bytesRet = 0;

					BOOL b = DeviceIoControl(hFile, FSCTL_GET_RETRIEVAL_POINTERS,
						&start_vcn, sizeof(start_vcn), ret_ptrs, static_cast<DWORD>(ret_buf.size()), &bytesRet, NULL);

					DWORD err = GetLastError();

					if (b || err == ERROR_MORE_DATA)
					{
						LARGE_INTEGER last_vcn = ret_ptrs->StartingVcn;
						for (DWORD i = 0; i<ret_ptrs->ExtentCount; ++i)
						{
							if (ret_ptrs->Extents[i].Lcn.QuadPart != -1)
							{
								int64 count = ret_ptrs->Extents[i].NextVcn.QuadPart - last_vcn.QuadPart;

								sector_count += countSectors(ret_ptrs->Extents[i].Lcn.QuadPart, count, fc_bitmap);
								total_sectors += count;
							}

							last_vcn = ret_ptrs->Extents[i].NextVcn;
						}
					}

					if (!b)
					{
						if (err == ERROR_MORE_DATA)
						{
							start_vcn.StartingVcn = ret_ptrs->Extents[ret_ptrs->ExtentCount - 1].NextVcn;
						}
						else if (err == ERROR_HANDLE_EOF)
						{
							CloseHandle(hFile);
							break;
						}
						else
						{
							Server->Log("Error " + convert((int)GetLastError()) + " while accessing retrieval points", LL_WARNING);
							CloseHandle(hFile);
							break;
						}
					}
					else
					{
						CloseHandle(hFile);
						break;
					}
				}

				if (sector_count > 0)
				{
					++total_files;
					total_changed_sectors += sector_count;
				}
				
				if (sector_count > 0
					&& sector_count*getBlocksize()>min_size)
				{
					Server->Log("Changed in " + volpath + os_file_sep() + files[i].name + ": " + convert(sector_count) + "/"+convert(total_sectors)+" sectors " + PrettyPrintBytes(sector_count*getBlocksize())
						+"/"+ PrettyPrintBytes(total_sectors*getBlocksize()), LL_INFO);
				}
			}
			else
			{
				Server->Log("Error opening file: " + volpath + os_file_sep() + files[i].name, LL_INFO);
			}
		}
	}
}

int64 FSNTFSWIN::countSectors(int64 start, int64 count, char* fc_bitmap)
{
	int64 ret = 0;
	for (int64 block = start; block<start + count; ++block)
	{
		size_t bitmap_byte = (size_t)(block / 8);
		size_t bitmap_bit = block % 8;

		unsigned char b = fc_bitmap[bitmap_byte];

		if ((b & (1 << bitmap_bit))>0)
		{
			++ret;
		}
	}

	return ret;
}

std::string FSNTFSWIN::getType()
{
	return "ntfs_win";
}
