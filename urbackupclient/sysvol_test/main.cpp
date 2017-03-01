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

#include "../win_sysvol.h"
#include <iostream>
#include "../../stringtools.h"
#include <Windows.h>

#define DEF_SERVER
#include "../../Interface/Server.h"
IServer *Server = NULL;

#include "../../urbackupcommon/server_compat.h"

int main(void)
{
	std::string mpath;
	std::string sysvol=getSysVolume(mpath);

	if(!sysvol.empty())
	{
		std::cout << "Found sysvol \"" << sysvol << "\"" <<
			" Path: \"" << mpath << "\"" << std::endl;
	}
	else
	{
		std::cout << "No sysvolume found." << std::endl;
	}

	std::cout << "Trying to open SYSVOL(" << sysvol << ")..." << std::endl;

	HANDLE hVolume=CreateFileW(ConvertToWchar(sysvol).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		std::cout << "Could not open SYSVOL(" << sysvol << "). Last error=" << GetLastError() << std::endl;
	}
	else
	{
		std::cout << "Successfully opened SYSVOL." << std::endl;

		char buf[512];
		DWORD read;
		if (ReadFile(hVolume, buf, 512, &read, NULL) == FALSE
			|| read != 512)
		{
			std::cout << "Error reading from SYSVOL. Error: " << GetLastError() << std::endl;
		}

		CloseHandle(hVolume);
	}

	sysvol=getEspVolume(mpath);

	if(!sysvol.empty())
	{
		std::cout << "Found EFI System Partition \"" << sysvol << "\"" <<
			" Path: \"" << mpath << "\"" << std::endl;
	}
	else
	{
		std::cout << "No EFI System Partition found." << std::endl;
	}

	std::cout << "Trying to open EFI System Partition(" << sysvol << ")..." << std::endl;

	hVolume=CreateFileW(ConvertToWchar(sysvol).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		std::cout << "Could not open EFI System Partition(" << sysvol << "). Last error=" << GetLastError() << std::endl;
	}
	else
	{
		std::cout << "Successfully opened EFI System Partition." << std::endl;
		CloseHandle(hVolume);
	}


	char ch;
	std::cin >> ch;
}