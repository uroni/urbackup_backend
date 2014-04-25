#include "win_all_volumes.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <Windows.h>

namespace
{
	std::string get_volume_path(PWCHAR VolumeName)
	{		
		PWCHAR names = NULL;		
		DWORD CharCount = MAX_PATH + 1;
		std::wstring ret;

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
					ret = cname;
				}
			}
		}

		if ( names != NULL )
		{
			delete [] names;
			names = NULL;
		}

		return Server->ConvertToUTF8(ret);
	}
}

std::string get_all_volumes_list()
{
	WCHAR vol_name[MAX_PATH] = {};
	HANDLE hFind = FindFirstVolumeW(vol_name, ARRAYSIZE(vol_name));

	if (hFind == INVALID_HANDLE_VALUE)
	{
		return "";
	}

	std::string ret;

	while(true)
	{
		size_t idx = wcslen(vol_name) - 1;

		if (vol_name[0] != L'\\' || vol_name[1] != L'\\' ||
			vol_name[2] != L'?'  ||	vol_name[3] != L'\\' ||
			vol_name[idx] != L'\\' )
		{
			Server->Log(std::wstring(L"get_all_volumes_list: bad path: ") + vol_name, LL_ERROR);
			return "";
		}

		vol_name[idx] = L'\0';

		WCHAR DeviceName[MAX_PATH] = {};
		DWORD CharCount = QueryDosDeviceW(&vol_name[4], DeviceName, ARRAYSIZE(DeviceName));

		vol_name[idx] = L'\\';

		if ( CharCount == 0 )
		{
			Server->Log(std::wstring(L"QueryDosDeviceW failed with ec = ") + convert(static_cast<int>(GetLastError())), LL_ERROR);
			break;
		}

		std::string new_volume = get_volume_path(vol_name);

		if(new_volume.size()==3 && new_volume[1]==':' && new_volume[2]=='\\')
		{
			if(!ret.empty())
			{
				ret+=";";
			}

			ret+=new_volume[0];
		}

		BOOL rc = FindNextVolumeW(hFind, vol_name, ARRAYSIZE(vol_name));
		if ( !rc )
		{
			if (GetLastError() != ERROR_NO_MORE_FILES)
			{
				Server->Log(std::wstring(L"FindNextVolumeW failed with ec = ") + convert(static_cast<int>(GetLastError())), LL_ERROR);
				break;
			}
			break;
		}
	}

	FindVolumeClose(hFind);	

	return ret;
}

