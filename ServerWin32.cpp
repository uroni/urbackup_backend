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
#define __ICondition_INTERFACE_DEFINED__
#include "vld.h"
#include "Server.h"
#include "stringtools.h"
#include <windows.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Strsafe.h>

bool CServer::LoadDLL(const std::string &name)
{
	HMODULE dll = LoadLibraryA( name.c_str() );

	if(dll==NULL)
	{
		Server->Log("Loading DLL \""+name+"\" failed. Error Code: "+convert((int)GetLastError()));
		return false;
	}

	unload_handles.push_back( dll );

	LOADACTIONS load_func=NULL;
	load_func=(LOADACTIONS)GetProcAddress(dll,"LoadActions");
	unload_functs.insert(std::pair<std::string, UNLOADACTIONS>(name, (UNLOADACTIONS) GetProcAddress(dll,"UnloadActions") ) );

	if(load_func==NULL)
		return false;

	load_func(this);
	return true;
}

void CServer::UnloadDLLs2(void)
{
	for(size_t i=0;i<unload_handles.size();++i)
	{
		FreeLibrary( unload_handles[i] );
	}
}

int CServer::WriteDump(void* pExceptionPointers)
{
	BOOL bMiniDumpSuccessful;
    WCHAR szPath[MAX_PATH]; 
    WCHAR szFileName[MAX_PATH]; 
    WCHAR* szAppName = L"UrBackup";
    WCHAR* szVersion = L"v2.0.0";
    DWORD dwBufferSize = MAX_PATH;
    HANDLE hDumpFile;
    SYSTEMTIME stLocalTime;
    MINIDUMP_EXCEPTION_INFORMATION ExpParam;

    GetLocalTime( &stLocalTime );
    GetTempPath( dwBufferSize, szPath );

    StringCchPrintf( szFileName, MAX_PATH, L"%s%s", szPath, szAppName );
    CreateDirectory( szFileName, NULL );

    StringCchPrintf( szFileName, MAX_PATH, L"%s%s\\%s-%04d%02d%02d-%02d%02d%02d-%ld-%ld.dmp", 
               szPath, szAppName, szVersion, 
               stLocalTime.wYear, stLocalTime.wMonth, stLocalTime.wDay, 
               stLocalTime.wHour, stLocalTime.wMinute, stLocalTime.wSecond, 
               GetCurrentProcessId(), GetCurrentThreadId());
    hDumpFile = CreateFile(szFileName, GENERIC_READ|GENERIC_WRITE, 
                FILE_SHARE_WRITE|FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	if(hDumpFile!=INVALID_HANDLE_VALUE)
	{
		ExpParam.ThreadId = GetCurrentThreadId();
		ExpParam.ExceptionPointers = (EXCEPTION_POINTERS*)pExceptionPointers;
		ExpParam.ClientPointers = TRUE;

		bMiniDumpSuccessful = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), 
						hDumpFile, MiniDumpWithFullMemory, &ExpParam, NULL, NULL);

		if(!bMiniDumpSuccessful)
		{
			Server->Log("Writing minidump failed: Last error="+convert((int)GetLastError()), LL_ERROR);
		}

		CloseHandle(hDumpFile);
	}

	Server->Log("Fatal exception (APPLICATION CRASHED). Crash dump written to \""+Server->ConvertFromWchar(szFileName)+"\"", LL_ERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}