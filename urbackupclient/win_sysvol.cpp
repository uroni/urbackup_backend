#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include "../stringtools.h"
#include "../log_redir.h"
#include "../Interface/Mutex.h"
#include "../Interface/Thread.h"
#include "../Interface/Server.h"

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

DWORD getDevNum(const PWCHAR VolumeName, DWORD& device_type)
{
	HANDLE hVolume=CreateFileW(VolumeName, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		return -1;
	}

	STORAGE_DEVICE_NUMBER dev_num;
	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(STORAGE_DEVICE_NUMBER), &ret_bytes, NULL);
	CloseHandle(hVolume);
	if(b==0)
	{
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
		LOG(L"Error opening device for reading bootable flag (Volume="+std::wstring(VolumeName)+L")", LL_INFO);
		return false;
	}

	DWORD ret_bytes;
	PARTITION_INFORMATION_EX partition_information;
	BOOL b=DeviceIoControl(hVolume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &partition_information, sizeof(PARTITION_INFORMATION_EX), &ret_bytes, NULL);
	CloseHandle(hVolume);

	if(b==FALSE)
	{
		LOG(L"Error reading partition information for bootable flag (Volume="+std::wstring(VolumeName)+L")", LL_INFO);
		return false;
	}

	if(partition_information.PartitionStyle!=PARTITION_STYLE_MBR)
	{
		if(partition_information.PartitionStyle==PARTITION_STYLE_GPT)
		{
			LOG(L"GPT formated hard disk encountered. UrBackup does not support GPT formated hard disks! (Volume="+std::wstring(VolumeName)+L")", LL_ERROR);

			return false;
		}
		else
		{
			LOG(L"Unknown partition style encountered (Volume="+std::wstring(VolumeName)+L")", LL_INFO);

			return false;
		}
	}
	else
	{
		return partition_information.Mbr.BootIndicator==TRUE;
	}
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

std::wstring getSysVolume(std::wstring &mpath)
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
		LOG(L"FindFirstVolumeW failed with error code "+convert((int)Error), LL_ERROR);
        return L"";
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
            LOG(L"FindFirstVolumeW/FindNextVolumeW returned a bad path: "+(std::wstring)VolumeName, LL_ERROR);
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
            LOG(L"QueryDosDeviceW failed with error code "+convert((int)Error), LL_ERROR );
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

		LOG(L"Filesystem. Vol=\""+std::wstring(VolumeName)+L"\" Name=\""+strlower(getVolumeLabel(VolumeName))+
			L"\" Type=\""+strlower(getFilesystem(VolumeName))+L"\" VPaths="+convert(vpaths.size())+L" Size="+convert(getPartSize(VolumeName)), LL_DEBUG);

		if(is_c_drive)
		{
			LOG("Filesystem is System partition. Skipping...", LL_DEBUG);
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

			if( strlower(getVolumeLabel(VolumeName))==L"system reserved"
				|| strlower(getVolumeLabel(VolumeName))==L"system-reserviert" )
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
				LOG("Bootable flag set for volume", LL_DEBUG);
				candidate.score+=2;
			}
			else
			{
				LOG("Bootable flag not set for volume", LL_DEBUG);
			}
			VolumeName[Index] = L'\\';

			if(candidate.score>0)
			{
				LOG(L"Found potential candidate: "+std::wstring(VolumeName)+L" Score: "+convert(candidate.score), LL_DEBUG);			
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
                LOG(L"FindNextVolumeW failed with error code "+convert((int)Error));
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
		DWORD curr_dev_type;
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
				LOG(L"Different device type from 'C': "+system_vols[i].volname+(system_vols[i].volpath.empty()?L"":(L" ("+system_vols[i].volpath+L")")), LL_DEBUG);
			}
		}
		else
		{
			LOG(L"Not on Physical Device 'C': "+system_vols[i].volname+(system_vols[i].volpath.empty()?L"":(L" ("+system_vols[i].volpath+L")")), LL_DEBUG);
		}
	}

	if(selidx!=std::string::npos)
	{
		LOG(L"Selected volume "+system_vols[selidx].volname+(system_vols[selidx].volpath.empty()?L"":(L" ("+system_vols[selidx].volpath+L")")), LL_DEBUG);
		mpath=system_vols[selidx].volpath;
		return system_vols[selidx].volname;
	}


	LOG("Found no SYSVOL on the same physical device as 'C'.", LL_INFO);

    return L"";
}

namespace
{
	std::wstring mpath_cached;
	std::wstring sysvol_name_cached;

	IMutex* mutex = NULL;

	class SysvolCacheThread : public IThread
	{
	public:
		void operator()()
		{
			{
				IScopedLock lock(mutex);
				sysvol_name_cached = getSysVolume(mpath_cached);
			}
			delete this;
		}
	};
}

std::wstring getSysVolumeCached(std::wstring &mpath)
{
	IScopedLock lock(mutex);

	mpath = mpath_cached;
	return sysvol_name_cached;
}

void cacheSysVolume()
{
	mutex = Server->createMutex();
	Server->createThread(new SysvolCacheThread);
}
