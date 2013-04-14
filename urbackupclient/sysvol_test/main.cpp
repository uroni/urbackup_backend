#include "../win_sysvol.h"
#include <iostream>
#include "../../stringtools.h"
#include <Windows.h>

int main(void)
{
	std::wstring mpath;
	std::wstring sysvol=getSysVolume(mpath);

	if(!sysvol.empty())
	{
		std::cout << "Found sysvol \"" << wnarrow(sysvol) << "\"" <<
			" Path: \"" << wnarrow(mpath) << "\"" << std::endl;
	}
	else
	{
		std::cout << "No sysvolume found." << std::endl;
	}

	std::cout << "Trying to open SYSVOL(" << wnarrow(sysvol) << ")..." << std::endl;

	HANDLE hVolume=CreateFileW(sysvol.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		std::cout << "Could not open SYSVOL(" << wnarrow(sysvol) << "). Last error=" << GetLastError() << std::endl;
	}
	else
	{
		std::cout << "Successfully opened SYSVOL." << std::endl;
		CloseHandle(hVolume);
	}

	char ch;
	std::cin >> ch;
}