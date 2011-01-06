/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "../vld.h"
#include "piped_process.h"
#include "../Interface/Server.h"
#include "../stringtools.h"

#define BUFSIZE 1000

CPipedProcess::CPipedProcess(std::string cmd)
{
	is_open=true;
	stop_thread=false;
	thread_stopped=false;
	output=Server->createMemoryPipe();

	SECURITY_ATTRIBUTES saAttr; 

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) ) 
		Server->Log("Error creating PIPE g_hChildStd_OUT_Rd", LL_ERROR);

	if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) )
      Server->Log("Error creating SetHandleInformation g_hChildStd_OUT_Rd", LL_ERROR);

	if (! CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0)) 
		Server->Log("Error creating PIPE g_hChildStd_IN_Rd", LL_ERROR);

	if ( ! SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0) )
		Server->Log("Error creating SetHandleInformation g_hChildStd_IN_Wr", LL_ERROR);

	TCHAR szCmdline[]=TEXT("child");
	STARTUPINFOA siStartInfo;
	BOOL bSuccess = FALSE; 

	ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );

	ZeroMemory( &siStartInfo, sizeof(STARTUPINFOA) );
	siStartInfo.cb = sizeof(STARTUPINFOA); 
	siStartInfo.hStdError = g_hChildStd_OUT_Wr;
	siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
	siStartInfo.hStdInput = g_hChildStd_IN_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	Server->Log("Creating process", LL_DEBUG);

	bSuccess = CreateProcessA(NULL, (char*)cmd.c_str(), &saAttr, NULL,TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo);

	if( !bSuccess )
	{
		Server->Log("Error Creating Process", LL_ERROR);
		is_open=false;
		return;
	}
	Server->Log("Created process", LL_DEBUG);
}

CPipedProcess::~CPipedProcess()
{
	Server->Log("Closing..", LL_DEBUG);
	stop_thread=true;
	CloseHandle(g_hChildStd_OUT_Rd);
	CloseHandle(g_hChildStd_IN_Rd);
	CloseHandle(g_hChildStd_OUT_Wr);
	CloseHandle(g_hChildStd_IN_Wr);
	BOOL rc=TerminateProcess(piProcInfo.hProcess, 0);
	if(!rc )
	{
		Server->Log("Error Terminating Process "+nconvert((int)GetLastError()), LL_ERROR);
	}
	CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);

	std::string tmp;
	while(thread_stopped==false)
		output->isReadable(-1);

	Server->destroy(output);
	Server->Log("Closing... CPipedProcess", LL_DEBUG);
}

bool CPipedProcess::Write(const std::string &str)
{
	 BOOL bSuccess = FALSE;
	 DWORD dwWritten;
	
	 bSuccess = WriteFile(g_hChildStd_IN_Wr, str.c_str(), str.size(), &dwWritten, NULL);

	 if( dwWritten!=str.size() )
	 {
		 is_open=false;
		 stop_thread=true;
		 Server->Log("Not enough buffer space", LL_ERROR);
		 return false;
	 }

	 if ( ! bSuccess ) return false; 
	 else
	 {
		 unsigned long ExitCode=0;
		 if(!GetExitCodeProcess(piProcInfo.hProcess , &ExitCode) || ExitCode!= STILL_ACTIVE)
		 {
			 is_open=false;
			 stop_thread=true;
			 return false;
		 }

		 return true;
	 }
}

std::string CPipedProcess::Read(void)
{
	if(g_hChildStd_OUT_Wr!=NULL)
	{
		CloseHandle(g_hChildStd_OUT_Wr);
		g_hChildStd_OUT_Wr=NULL;
	}

	DWORD dwRead;
	BOOL bSuccess = FALSE;
	CHAR chBuf[BUFSIZE];

	bSuccess = ReadFile( g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL);

	if( !bSuccess )
	{
		is_open=false;
		Server->Log("Error reading STD_OUT", LL_ERROR );
		return "";
	}

	std::string ret;
	ret.resize(dwRead);
	memcpy((void*)ret.c_str(), chBuf, dwRead);

	if(ret.size()==0)
		return Read();	

	return ret;
}
