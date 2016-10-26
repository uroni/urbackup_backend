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

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include "../stringtools.h"
#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"
#include "../urbackupcommon/server_compat.h"

namespace
{
	void Log(const std::wstring& pStr, int loglevel)
	{
		Log(ConvertFromWchar(pStr), loglevel);
	}
}

std::wstring getVolumeLabel(PWCHAR VolumeName)
{
	wchar_t voln[MAX_PATH+1];
	DWORD voln_size=MAX_PATH+1;
	DWORD voln_sern;
	wchar_t fsn[MAX_PATH+1];
	DWORD fsn_size=MAX_PATH+1;
	BOOL b=GetVolumeInformationW(VolumeName, voln, voln_size, &voln_sern, NULL, NULL, fsn, fsn_size);
	if(b==0)
	{
		return L"";
	}
	
	return voln;
}

std::wstring getFilesystem(PWCHAR VolumeName)
{
	wchar_t voln[MAX_PATH+1];
	DWORD voln_size=MAX_PATH+1;
	DWORD voln_sern;
	wchar_t fsn[MAX_PATH+1];
	DWORD fsn_size=MAX_PATH+1;
	BOOL b=GetVolumeInformationW(VolumeName, voln, voln_size, &voln_sern, NULL, NULL, fsn, fsn_size);
	if(b==0)
	{
		return L"";
	}
	
	return fsn;
}

DWORD getDevNum(std::wstring VolumeName, DWORD& device_type)
{
	HANDLE hVolume=CreateFileW(VolumeName.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		Log("Cannot open volume", LL_DEBUG);
		return -1;
	}

	STORAGE_DEVICE_NUMBER dev_num;
	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(STORAGE_DEVICE_NUMBER), &ret_bytes, NULL);
	CloseHandle(hVolume);
	if(b==0)
	{
		Log("Cannot get storage device number", LL_DEBUG);
		return -1;
	}

	device_type=dev_num.DeviceType;
	return dev_num.DeviceNumber;
}

DWORD getPartNum(const PWCHAR VolumeName)
{
	HANDLE hVolume=CreateFileW(VolumeName, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		return 0;
	}

	STORAGE_DEVICE_NUMBER dev_num;
	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(STORAGE_DEVICE_NUMBER), &ret_bytes, NULL);
	CloseHandle(hVolume);
	if(b==0)
	{
		return 0;
	}

	return dev_num.PartitionNumber;
}

__int64 getPartSize(const PWCHAR VolumeName)
{

	ULARGE_INTEGER li;
	BOOL b=GetDiskFreeSpaceExW(VolumeName, NULL, &li, NULL);
	if(b==0)
	{
		return -1;
	}	

	return li.QuadPart;
}

std::vector<std::wstring> GetVolumePaths(PWCHAR VolumeName)
{
    DWORD  CharCount = MAX_PATH + 1;
    PWCHAR Names     = NULL;
    PWCHAR NameIdx   = NULL;
    BOOL   Success   = FALSE;
	std::vector<std::wstring> ret;

    for (;;) 
    {
        Names = (PWCHAR) new BYTE [CharCount * sizeof(WCHAR)];
        if ( !Names ) 
        {
            return ret;
        }
		Success = GetVolumePathNamesForVolumeNameW(
            VolumeName, Names, CharCount, &CharCount
            );
        if ( Success ) 
        {
            break;
        }
        if ( GetLastError() != ERROR_MORE_DATA ) 
        {
            break;
        }
        delete [] Names;
        Names = NULL;
    }

    if ( Success )
    {
        for ( NameIdx = Names; 
              NameIdx[0] != L'\0'; 
              NameIdx += wcslen(NameIdx) + 1 ) 
        {
			ret.push_back(NameIdx);
        }
    }

    if ( Names != NULL ) 
    {
        delete [] Names;
        Names = NULL;
    }

    return ret;
}

bool isBootable(const PWCHAR VolumeName)
{
	HANDLE hVolume=CreateFileW(VolumeName, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		Log(L"Error opening device for reading bootable flag (Volume="+std::wstring(VolumeName)+L")", LL_INFO);
		return false;
	}

	DWORD ret_bytes;
	PARTITION_INFORMATION_EX partition_information;
	BOOL b=DeviceIoControl(hVolume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partition_information, sizeof(PARTITION_INFORMATION_EX), &ret_bytes, NULL);
	CloseHandle(hVolume);

	if(b==FALSE)
	{
		Log(L"Error reading partition information for bootable flag (Volume="+std::wstring(VolumeName)+L")", LL_INFO);
		return false;
	}

	if(partition_information.PartitionStyle!=PARTITION_STYLE_MBR)
	{
		if(partition_information.PartitionStyle==PARTITION_STYLE_GPT)
		{
			Log(L"GPT formated hard disk encountered. No bootable flag. Attributes = "+ConvertToWchar(convert((int64)partition_information.Gpt.Attributes)), LL_DEBUG);

			if(partition_information.Gpt.Attributes & (DWORD64)1<<63)
			{
				Log("Do not automount is set", LL_DEBUG);
			}

			if(partition_information.Gpt.Attributes & (DWORD64)1<<62)
			{
				Log("Hidden is set", LL_DEBUG);
			}

			if(partition_information.Gpt.Attributes & (DWORD64)1<<1)
			{
				Log("EFI firmware ignore is set", LL_DEBUG);
			}

			if(partition_information.Gpt.Attributes & (DWORD64)1<<2)
			{
				Log("Legacy bios bootable is set", LL_DEBUG);
			}

			if(partition_information.Gpt.Attributes & (DWORD64)1<<0)
			{
				Log("System partition is set", LL_DEBUG);
			}

			return (partition_information.Gpt.Attributes & (DWORD64)1<<0) &&
				(partition_information.Gpt.Attributes & (DWORD64)1<<63);
		}
		else
		{
			Log(L"Unknown partition style encountered (Volume="+std::wstring(VolumeName)+L")", LL_ERROR);

			return false;
		}
	}
	else
	{
		return partition_information.Mbr.BootIndicator==TRUE;
	}
}

std::string findGptUuid(int device_num, GUID uuid)
{
	HANDLE hDevice=CreateFileW((L"\\\\.\\PhysicalDrive"+ConvertToWchar(convert(device_num))).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hDevice==INVALID_HANDLE_VALUE)
	{
		Log("CreateFile of device '"+convert(device_num)+"' failed.", LL_ERROR);
		return std::string();
	}

	DWORD numPartitions=10;
	DWORD inf_size=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1);

	std::auto_ptr<DRIVE_LAYOUT_INFORMATION_EX> inf((DRIVE_LAYOUT_INFORMATION_EX*)new char[sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1)]);

	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, inf.get(), inf_size, &ret_bytes, NULL);
	while(b==0 && GetLastError()==ERROR_INSUFFICIENT_BUFFER && numPartitions<1000)
	{
		numPartitions*=2;
		inf_size=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1);
		inf.reset((DRIVE_LAYOUT_INFORMATION_EX*)new char[inf_size]);
		b=DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, inf.get(), inf_size, &ret_bytes, NULL);
	}
	CloseHandle(hDevice);
	if(b==0)
	{
		Log("DeviceIoControl IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed. Device: '"+convert(device_num)+"' Error: "+convert((int)GetLastError()), LL_ERROR);		
		return std::string();
	}

	if(inf->PartitionStyle!=PARTITION_STYLE_GPT)
	{
		Log("Device is not GPT formatted ("+convert(device_num)+")", LL_DEBUG);
		return std::string();
	}

	for(DWORD i=0;i<inf->PartitionCount;++i)
	{
		LPOLESTR uuid_str;
		if(StringFromCLSID(inf->PartitionEntry[i].Gpt.PartitionType, &uuid_str)==NOERROR)
		{
			Log(L"EFI partition with type UUID "+std::wstring(uuid_str), LL_DEBUG);
			CoTaskMemFree(uuid_str);
		}

		if(memcmp(&inf->PartitionEntry[i].Gpt.PartitionType, &uuid, sizeof(uuid))==0)
		{
			return "\\\\?\\GLOBALROOT\\Device\\Harddisk"+convert(device_num)+"\\Partition"+convert((int)i+1);
		}
	}

	return std::string();
}

namespace
{
	struct SSysvolCandidate
	{
		SSysvolCandidate()
			: score(0) 
		{}

		size_t score;
		std::wstring volname;
		std::wstring volpath;
	};
}

std::string getSysVolume(std::string &mpath)
{
    DWORD  CharCount            = 0;
    WCHAR  DeviceName[MAX_PATH] = L"";
    DWORD  Error                = ERROR_SUCCESS;
    HANDLE FindHandle           = INVALID_HANDLE_VALUE;
    BOOL   Found                = FALSE;
    size_t Index                = 0;
    BOOL   Success              = FALSE;
    WCHAR  VolumeName[MAX_PATH] = L"";
	WCHAR  VolumeNameNt[MAX_PATH] = L"";

    FindHandle = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));

    if (FindHandle == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
		Log("FindFirstVolumeW failed with error code "+convert((int)Error), LL_ERROR);
        return "";
    }

	DWORD c_drive_num=-1;
	DWORD c_drive_type=-1;
	std::vector<SSysvolCandidate> system_vols;

    for (;;)
    {
        Index = wcslen(VolumeName) - 1;

        if (VolumeName[0]     != L'\\' ||
            VolumeName[1]     != L'\\' ||
            VolumeName[2]     != L'?'  ||
            VolumeName[3]     != L'\\' ||
            VolumeName[Index] != L'\\') 
        {
            Error = ERROR_BAD_PATHNAME;
            Log(L"FindFirstVolumeW/FindNextVolumeW returned a bad path: "+(std::wstring)VolumeName, LL_ERROR);
            break;
        }

		memcpy(VolumeNameNt, VolumeName, sizeof(WCHAR)*(Index+1));

        VolumeName[Index] = L'\0';
		VolumeNameNt[Index]= L'\0';

        CharCount = QueryDosDeviceW(&VolumeName[4], DeviceName, ARRAYSIZE(DeviceName)); 

        VolumeName[Index] = L'\\';

        if ( CharCount == 0 ) 
        {
            Error = GetLastError();
            Log("QueryDosDeviceW failed with error code "+convert((int)Error), LL_ERROR );
            break;
        }

		bool is_c_drive=false;

		std::vector<std::wstring> vpaths=GetVolumePaths(VolumeName);
		VolumeName[Index] = L'\0';
		for(size_t i=0;i<vpaths.size();++i)
		{
			if(vpaths[i]==L"C:\\")
			{
				c_drive_num=getDevNum(VolumeName, c_drive_type);
				is_c_drive=true;
			}
		}
		VolumeName[Index] = L'\\';

		Log(L"Filesystem. Vol=\""+std::wstring(VolumeName)+L"\" Name=\""+ConvertToWchar(strlower(ConvertFromWchar(getVolumeLabel(VolumeName))))+
			L"\" Type=\""+ConvertToWchar(strlower(ConvertFromWchar(getFilesystem(VolumeName))))+L"\" VPaths="+ConvertToWchar(convert(vpaths.size()))+L" Size="+ConvertToWchar(convert(getPartSize(VolumeName))), LL_DEBUG);

		if(is_c_drive)
		{
			Log("Filesystem is System partition. Skipping...", LL_DEBUG);
		}
		else
		{		
			SSysvolCandidate candidate;
			VolumeName[Index] = L'\0';
			candidate.volname=VolumeName;
			VolumeName[Index] = L'\\';

			if(!vpaths.empty())
			{
				candidate.volpath=vpaths[0];
			}

			if( strlower(ConvertFromWchar(getVolumeLabel(VolumeName)))=="system reserved"
				|| strlower(ConvertFromWchar(getVolumeLabel(VolumeName)))=="system-reserviert" )
			{
				candidate.score+=100;
			}

			if( vpaths.empty() )
			{
				++candidate.score;
			}

			__int64 partsize = getPartSize(VolumeName);
			if(partsize>0 && partsize<200*1024*1024 )
			{
				candidate.score+=2;
			}

			VolumeName[Index] = L'\0';
			if( isBootable(VolumeName) )
			{
				Log("Bootable flag set for volume", LL_DEBUG);
				candidate.score+=2;
			}
			else
			{
				Log("Bootable flag not set for volume", LL_DEBUG);
			}
			VolumeName[Index] = L'\\';

			if(candidate.score>0)
			{
				Log(L"Found potential candidate: "+std::wstring(VolumeName)+L" Score: "+ConvertToWchar(convert(candidate.score)), LL_DEBUG);			
				system_vols.push_back(candidate);
			}
		}
		
		VolumeName[Index] = L'\\';

        Success = FindNextVolumeW(FindHandle, VolumeName, ARRAYSIZE(VolumeName));

        if ( !Success ) 
        {
            Error = GetLastError();

            if (Error != ERROR_NO_MORE_FILES) 
            {
                Log("FindNextVolumeW failed with error code "+convert((int)Error), LL_INFO);
                break;
            }

			Error = ERROR_SUCCESS;
            break;
        }
    }

    FindVolumeClose(FindHandle);
    FindHandle = INVALID_HANDLE_VALUE;

	size_t max_score=0;
	size_t selidx=std::string::npos;

	for(size_t i=0;i<system_vols.size();++i)
	{
		DWORD curr_dev_type = -1;
		DWORD curr_dev_num=getDevNum((PWCHAR)system_vols[i].volname.c_str(), curr_dev_type);
		if(curr_dev_num==c_drive_num)
		{
			if(curr_dev_type==c_drive_type)
			{
				if(system_vols[i].score>max_score)
				{
					selidx=i;
					max_score=system_vols[i].score;
				}
			}
			else
			{
				Log(L"Different device type from 'C': "+system_vols[i].volname+(system_vols[i].volpath.empty()?L"":(L" ("+system_vols[i].volpath+L")")), LL_DEBUG);
			}
		}
		else
		{
			Log(L"Not on Physical Device 'C': "+system_vols[i].volname+(system_vols[i].volpath.empty()?L"":(L" ("+system_vols[i].volpath+L")")), LL_DEBUG);
		}
	}

	if(selidx!=std::string::npos)
	{
		Log(L"Selected volume "+system_vols[selidx].volname+(system_vols[selidx].volpath.empty()?L"":(L" ("+system_vols[selidx].volpath+L")")), LL_DEBUG);
		mpath=ConvertFromWchar(system_vols[selidx].volpath);
		return ConvertFromWchar(system_vols[selidx].volname);
	}


	Log("Found no SYSVOL on the same physical device as 'C'.", LL_INFO);

    return "";
}

std::string getEspVolume( std::string &mpath )
{
	//GPT partition with UUID esp_uuid
	GUID esp_uuid;
	if(CLSIDFromString(L"{C12A7328-F81F-11D2-BA4B-00A0C93EC93B}", &esp_uuid)!=NOERROR)
	{
		Log("Error converting ESP uuid", LL_ERROR);
		return std::string();
	}

	WCHAR sysdir[MAX_PATH];
	if(GetWindowsDirectoryW(sysdir, MAX_PATH)==0)
	{
		Log("Error getting system dir: "+convert((int)GetLastError()), LL_ERROR);
		return std::string();
	}

	Log(L"System dir: "+std::wstring(sysdir), LL_DEBUG);

	std::wstring volpath;
	if(sysdir[0]!=0)
	{
		volpath = std::wstring(L"\\\\.\\")+sysdir[0]+L":";
	}

	Log(L"Volpath: "+volpath, LL_DEBUG);

	DWORD device_type;
	DWORD dev_num = getDevNum(volpath.c_str(), device_type);

	if(dev_num==-1)
	{
		Log("Error getting device number of system directory", LL_ERROR);
		return std::string();
	}

	std::string found_part = findGptUuid(dev_num, esp_uuid);
	
	if(found_part.empty())
	{
		Log("Found no EFI System Partition", LL_INFO);
	}
	else
	{
		Log("EFI System Partition is at "+found_part, LL_INFO);
	}

	return found_part;
}

namespace
{
	std::string mpath_cached;
	std::string sysvol_name_cached;

	std::string esp_mpath_cached;
	std::string esp_name_cached;

	IMutex* mutex = NULL;

	class SysvolCacheThread : public IThread
	{
	public:
		void operator()()
		{
			{
				IScopedLock lock(mutex);
				sysvol_name_cached = getSysVolume(mpath_cached);
				esp_name_cached = getEspVolume(esp_mpath_cached);
			}
			delete this;
		}
	};
}

std::string getSysVolumeCached(std::string &mpath)
{
	IScopedLock lock(mutex);

	mpath = mpath_cached;
	return sysvol_name_cached;
}

void cacheVolumes()
{
	mutex = Server->createMutex();
	Server->createThread(new SysvolCacheThread, "sysvol cache");
}

std::string getEspVolumeCached(std::string &mpath)
{
	IScopedLock lock(mutex);

	mpath = esp_mpath_cached;
	return esp_name_cached;
}
