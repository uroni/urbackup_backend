#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <iostream>
#include "../stringtools.h"
#include "../log_redir.h"

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

DWORD getDevNum(const PWCHAR VolumeName)
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
		return 0;
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

bool getSystemReservedVolume(void)
{
	HANDLE FindHandle=INVALID_HANDLE_VALUE;
	WCHAR  VolumeName[MAX_PATH] = L"";

	FindHandle = FindFirstVolumeW(VolumeName, ARRAYSIZE(VolumeName));
	if (FindHandle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

	for (;;)
    {
		size_t Index = wcslen(VolumeName) - 1;
		if (VolumeName[0]     != L'\\' ||
            VolumeName[1]     != L'\\' ||
            VolumeName[2]     != L'?'  ||
            VolumeName[3]     != L'\\' ||
            VolumeName[Index] != L'\\') 
        {
            return false;
        }

		VolumeName[Index] = L'\0';

		DWORD  CharCount=0;
		WCHAR  DeviceName[MAX_PATH]=L"";
        CharCount = QueryDosDeviceW(&VolumeName[4], DeviceName, ARRAYSIZE(DeviceName)); 

        VolumeName[Index] = L'\\';

		if ( CharCount == 0 ) 
        {
			return false;
		}

		BOOL Success = FindNextVolumeW(FindHandle, VolumeName, ARRAYSIZE(VolumeName));

        if ( !Success ) 
        {
            DWORD Error = GetLastError();

            if (Error != ERROR_NO_MORE_FILES) 
            {
				return false;
            }

            break;
        }
	}
	FindVolumeClose(FindHandle);

    return true;
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
	std::vector<std::wstring> system_vols;
	std::vector<std::wstring> system_vols_paths;

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

		std::vector<std::wstring> vpaths=GetVolumePaths(VolumeName);
		VolumeName[Index] = L'\0';
		for(size_t i=0;i<vpaths.size();++i)
		{
			if(vpaths[i]==L"C:\\")
			{
				c_drive_num=getDevNum(VolumeName);
			}
		}
		VolumeName[Index] = L'\\';

		
		if( ( strlower(getVolumeLabel(VolumeName))==L"system reserved" 
			|| (vpaths.empty() &&  getPartSize(VolumeName)<200*1024*1024) ) 
			&& strlower(getFilesystem(VolumeName))==L"ntfs")
		{
			VolumeName[Index] = L'\0';
			system_vols.push_back(VolumeName);
			LOG(L"Found potential candidate: "+std::wstring(VolumeName), LL_DEBUG);
			VolumeName[Index] = L'\\';
			if(!vpaths.empty())
			{
				system_vols_paths.push_back(vpaths[0]);
			}
			else
			{
				system_vols_paths.push_back(L"");
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

	for(size_t i=0;i<system_vols.size();++i)
	{
		if(getDevNum((PWCHAR)system_vols[i].c_str())==c_drive_num)
		{
			LOG(L"Selected: "+system_vols[i], LL_DEBUG);
			mpath=system_vols_paths[i];
			return system_vols[i];
		}
	}

	LOG("Found no SYSVOL on the same physical device as 'C'.", LL_INFO);

    return L"";
}