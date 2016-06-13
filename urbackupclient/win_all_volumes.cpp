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

#include "win_all_volumes.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <Windows.h>
#include "../Interface/File.h"

struct SVolumesCache
{
	std::map<std::string, bool> is_ntfs_info;
	std::map<std::string, bool> is_usb_info;
};

namespace
{
	std::string get_volume_path(PWCHAR VolumeName)
	{		
		PWCHAR names = NULL;		
		DWORD CharCount = MAX_PATH + 1;
		std::string ret;

		BOOL rc;
		while(true)
		{
			names = (PWCHAR) new BYTE [CharCount * sizeof(WCHAR)];

			if ( !names )
			{
				return "";
			}

			rc = GetVolumePathNamesForVolumeNameW(
				VolumeName, names, CharCount, &CharCount);

			if ( rc ) 
			{
				break;
			}

			if ( GetLastError() != ERROR_MORE_DATA ) 
			{
				break;
			}

			delete [] names;
			names = NULL;
		}

		if ( rc )
		{
			PWCHAR name = NULL;
			for ( name = names; name[0] != 0; name += wcslen(name) + 1 ) 
			{
				std::wstring cname = name;
				if(ret.empty() || cname.size()<ret.size())
				{
					ret = Server->ConvertFromWchar(cname);
				}
			}
		}

		if ( names != NULL )
		{
			delete [] names;
			names = NULL;
		}

		return (ret);
	}

	bool is_usb_disk(std::string path, SVolumesCache* cache)
	{
		std::map<std::string, bool>::iterator it = cache->is_usb_info.find(path);

		if(it!=cache->is_usb_info.end())
		{
			return it->second;
		}

		HANDLE hVolume = CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);

		if ( hVolume != INVALID_HANDLE_VALUE )
		{
			DWORD dwOutBytes = 0; 
			STORAGE_PROPERTY_QUERY query;  

			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;

			char buffer[1024] = {}; 
			PSTORAGE_DEVICE_DESCRIPTOR dev_desc = (PSTORAGE_DEVICE_DESCRIPTOR)buffer;
			dev_desc->Size = sizeof(buffer);

			BOOL b = DeviceIoControl(hVolume,
				IOCTL_STORAGE_QUERY_PROPERTY,
				&query, sizeof(STORAGE_PROPERTY_QUERY),
				dev_desc, dev_desc->Size,
				&dwOutBytes,
				(LPOVERLAPPED)NULL);

			CloseHandle(hVolume);

			if ( b )
			{
				bool is_usb = dev_desc->BusType == BusTypeUsb;

				cache->is_usb_info[path] = is_usb;

				return is_usb;
			}
		}

		return false;
	}
}

std::string get_all_volumes_list(bool filter_usb, SVolumesCache*& cache)
{
	WCHAR vol_name[MAX_PATH] = {};
	HANDLE hFind = FindFirstVolumeW(vol_name, ARRAYSIZE(vol_name));

	if (hFind == INVALID_HANDLE_VALUE)
	{
		return "";
	}

	if(cache==NULL)
	{
		cache=new SVolumesCache;
	}

	std::string ret;

	while(true)
	{
		size_t idx = wcslen(vol_name) - 1;

		if (vol_name[0] != L'\\' || vol_name[1] != L'\\' ||
			vol_name[2] != L'?'  ||	vol_name[3] != L'\\' ||
			vol_name[idx] != L'\\' )
		{
			Server->Log(std::string("get_all_volumes_list: bad path: ") + Server->ConvertFromWchar(vol_name), LL_ERROR);
			return "";
		}

		vol_name[idx] = L'\0';

		WCHAR DeviceName[MAX_PATH] = {};
		DWORD CharCount = QueryDosDeviceW(&vol_name[4], DeviceName, ARRAYSIZE(DeviceName));

		vol_name[idx] = L'\\';

		if ( CharCount == 0 )
		{
			Server->Log(std::string("QueryDosDeviceW failed with ec = ") + convert(static_cast<int>(GetLastError())), LL_ERROR);
			break;
		}

		std::string new_volume = get_volume_path(vol_name);

		if(new_volume.size()==3 && new_volume[1]==':' && new_volume[2]=='\\')
		{
			std::map<std::string, bool>::iterator it_ntfs_info = cache->is_ntfs_info.find(new_volume);

			std::string dev_fn = std::string("\\\\.\\")+std::string(1, new_volume[0])+":";

			if(it_ntfs_info!=cache->is_ntfs_info.end())
			{
				if(it_ntfs_info->second)
				{
					if(!filter_usb || !is_usb_disk(dev_fn, cache))
					{
						if(!ret.empty())
						{
							ret+=";";
						}
						ret+=new_volume[0];
					}
					else if(filter_usb)
					{
						Server->Log("Device "+new_volume+" is connected via USB", LL_INFO);
					}
				}
				else
				{
					Server->Log("Device "+new_volume+" isn't NTFS formatted", LL_INFO);
				}
			}
			else
			{				
				std::auto_ptr<IFile> dev(Server->openFile(dev_fn, MODE_READ_DEVICE));

				if(!dev.get())
				{
					Server->Log("Opening device \"" + new_volume + "\" failed", LL_ERROR);
				}
				else
				{
					char buffer[1024];
					_u32 rc=dev->Read(buffer, 1024);
					if(rc!=1024)
					{
						Server->Log("Cannot read data from device ("+new_volume+")", LL_INFO);
						cache->is_ntfs_info[new_volume] = false;
					}
					else
					{
						bool is_ntfs = buffer[3]=='N' && buffer[4]=='T' && buffer[5]=='F' && buffer[6]=='S';

						if(!is_ntfs)
						{
							Server->Log("Device "+new_volume+" isn't NTFS formatted", LL_INFO);
						}
						else
						{
							if(!filter_usb || !is_usb_disk(dev_fn, cache))
							{
								if(!ret.empty())
								{
									ret+=";";
								}
								ret+=new_volume[0];
							}
							else if(!filter_usb)
							{
								Server->Log("Device "+new_volume+" is connected via USB", LL_INFO);
							}
						}

						cache->is_ntfs_info[new_volume] = is_ntfs;
					}				
				}
			}
			
		}

		BOOL rc = FindNextVolumeW(hFind, vol_name, ARRAYSIZE(vol_name));
		if ( !rc )
		{
			if (GetLastError() != ERROR_NO_MORE_FILES)
			{
				Server->Log(std::string("FindNextVolumeW failed with ec = ") + convert(static_cast<int>(GetLastError())), LL_ERROR);
				break;
			}
			break;
		}
	}

	FindVolumeClose(hFind);	

	return ret;
}

