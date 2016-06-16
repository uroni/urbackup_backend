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

#include "file_permissions.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

#ifdef _WIN32
#include <Windows.h>
#include <Sddl.h>
#include <Aclapi.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32

const TCHAR * szSD = TEXT("D:")       // Discretionary ACL
        //TEXT("(D;OICI;GA;;;WD)")     // Deny access to 
                                     // built-in guests
        TEXT("(A;OICI;GA;;;SY)") // Deny access to 
                                     // to authenticated 
                                     // users
        TEXT("(A;OICI;GA;;;BA)");    // Allow full control 
                                     // to administrators

bool change_file_permissions_admin_only(const std::string& filename)
{
#ifdef _DEBUG
	return true;
#else
	PSECURITY_DESCRIPTOR pSDCNV = NULL;

     BOOL b=ConvertStringSecurityDescriptorToSecurityDescriptor(
                szSD,
                SDDL_REVISION_1,
				&pSDCNV,
                NULL);

	 if(!b)
	 {
		 Server->Log("Error creating security descriptor", LL_ERROR);
		 return false;
	 }

	SECURITY_DESCRIPTOR sd = {};
	 DWORD sd_size = sizeof(sd);
	 PACL pDACL = NULL;
	 DWORD dwDACLSize = 0;
	 PACL pSACL = NULL;
	 DWORD dwSACLSize = 0;
	 DWORD dwOwnerSIDSize = 0;
	 DWORD dwGroupSIDSize = 0;


	 if (! MakeAbsoluteSD(pSDCNV, &sd, &sd_size, 
           pDACL, &dwDACLSize, 
           pSACL, &dwSACLSize, 
           NULL, &dwOwnerSIDSize, 
           NULL, &dwGroupSIDSize) ) {
 
 
		  pDACL = (PACL) GlobalAlloc(GPTR, dwDACLSize);
		  pSACL = (PACL) GlobalAlloc(GPTR, dwSACLSize);
 
		  if (! MakeAbsoluteSD(pSDCNV, &sd, &sd_size, pDACL, &dwDACLSize, 
				   pSACL, &dwSACLSize, NULL, &dwOwnerSIDSize, 
				   NULL, &dwGroupSIDSize) ) {
			Server->Log("Error: MakeAbsoluteSD", LL_ERROR);
			LocalFree(pSDCNV);
			GlobalFree(pDACL);
			GlobalFree(pSACL);
			return false;
		  }
	 }


	 bool ret=true;

	 DWORD rc = SetNamedSecurityInfoW(const_cast<wchar_t*>(Server->ConvertToWchar(filename).c_str()),
		 SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, pDACL, NULL);
	 if(rc!=ERROR_SUCCESS)
	 {
		 Server->Log("Error setting security information of file " + filename + ". rc: "+convert((int)rc), LL_ERROR);
		 ret=false;
	 }

	 GlobalFree(pDACL);
	 GlobalFree(pSACL);

	 LocalFree(pSDCNV);

	 return ret;
#endif //_DEBUG
}

bool write_file_only_admin(const std::string& data, const std::string& fn)
{
#ifdef _DEBUG
	writestring(data, fn);
	return true;
#else
	SECURITY_ATTRIBUTES  sa;      
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = FALSE;


     BOOL b=ConvertStringSecurityDescriptorToSecurityDescriptor(
                szSD,
                SDDL_REVISION_1,
				&(sa.lpSecurityDescriptor),
                NULL);

	 if(!b)
	 {
		 Server->Log("Error creating security descriptor", LL_ERROR);
		 return false;
	 }

	 HANDLE file = CreateFileW(Server->ConvertToWchar(fn).c_str(),
		 GENERIC_READ | GENERIC_WRITE, 0, &sa, CREATE_ALWAYS, 0, NULL);

	 if(file==INVALID_HANDLE_VALUE)
	 {
		 Server->Log("Error creating pw file", LL_ERROR);
		 LocalFree(sa.lpSecurityDescriptor);
		 return false;
	 }

	 DWORD written=0;
	 while(written<data.size())
	 {
		b = WriteFile(file, data.data()+written, static_cast<DWORD>(data.size())-written, &written, NULL);
		if(!b)
		{
			Server->Log("Error writing to pw file", LL_ERROR);
			CloseHandle(file);
			LocalFree(sa.lpSecurityDescriptor);
			return false;
		}
	 }

	 CloseHandle(file);
	 LocalFree(sa.lpSecurityDescriptor);
	 return true;
#endif //_DEBUG
}

#else

bool write_file_only_admin(const std::string& data, const std::string& fn)
{
	int fd = open(fn.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);

	if(fd==-1)
	{
		Server->Log("Error opening admin only file", LL_ERROR);
		return false;
	}

	ssize_t rc = write(fd, data.data(), data.size());

	if(rc<data.size())
	{
		Server->Log("Error writing to admin only file", LL_ERROR);
		close(fd);
		return false;
	}
	
	close(fd);
	return true;
}

bool change_file_permissions_admin_only(const std::string& filename)
{
	if(chmod(filename.c_str(), S_IRWXU)!=0)
	{
		Server->Log("Error setting file permissions to \""+filename+"\"", LL_ERROR);
		return false;
	}
	return true;
}

#endif