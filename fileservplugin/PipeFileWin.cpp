/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "PipeFile.h"

PipeFile::PipeFile(const std::string& pCmd)
	: PipeFileBase(pCmd),
	hStderr(INVALID_HANDLE_VALUE),
	hStdout(INVALID_HANDLE_VALUE)
{
	SECURITY_ATTRIBUTES saAttr = {}; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL; 

	HANDLE hStdoutW;
	HANDLE hStderrW;

	if(!CreatePipe(&hStdout, &hStdoutW, &saAttr, 0))
	{
		Server->Log("Error creating stdout pipe", LL_ERROR);
		has_error=true;
		return;
	}

	if(!SetHandleInformation(hStdout, HANDLE_FLAG_INHERIT, 0))
	{
		Server->Log("Error setting handle information on stdout pipe", LL_ERROR);
		has_error=true;
		return;
	}

	if(!CreatePipe(&hStderr, &hStderrW, &saAttr, 0))
	{
		Server->Log("Error creating stderr pipe", LL_ERROR);
		has_error=true;
		return;
	}

	if(!SetHandleInformation(hStderr, HANDLE_FLAG_INHERIT, 0))
	{
		Server->Log("Error setting handle information on stderr pipe", LL_ERROR);
		has_error=true;
		return;
	}

	ZeroMemory( &proc_info, sizeof(PROCESS_INFORMATION) );

	STARTUPINFOW start_info = {};
	start_info.cb = sizeof(STARTUPINFOW); 
	start_info.hStdError = hStderrW;
	start_info.hStdOutput = hStdoutW;
	start_info.dwFlags |= STARTF_USESTDHANDLES;

	BOOL b = CreateProcessW(NULL, const_cast<LPWSTR>(Server->ConvertToWchar(getFilename()).c_str()),
		&saAttr, NULL, TRUE, 0, NULL, NULL, &start_info,
		&proc_info);

	if(!b)
	{
		Server->Log("Error starting script \"" + getFilename() + "\"", LL_ERROR);
		has_error=true;
	}

	CloseHandle(hStderrW);
	CloseHandle(hStdoutW);

	init();
}

PipeFile::~PipeFile()
{
	
}

void PipeFile::forceExitWait()
{
	CloseHandle(hStdout);
	CloseHandle(hStderr);

	TerminateProcess(proc_info.hProcess, 0);

	CloseHandle(proc_info.hProcess);
	CloseHandle(proc_info.hThread);

	waitForExit();
}

bool PipeFile::getExitCode(int& exit_code)
{
	DWORD dwExitCode;
	BOOL b = GetExitCodeProcess(proc_info.hProcess, &dwExitCode);

	if(!b)
	{
		Server->Log("Error getting exit code of process", LL_ERROR);

		return false;
	}
	else
	{
		if( dwExitCode == STILL_ACTIVE )
		{
			Server->Log("Process is still active", LL_ERROR);
			return false;
		}

		exit_code = static_cast<int>(dwExitCode);

		return true;
	}
}

bool PipeFile::readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes)
{
	return readIntoBuffer(hStdout, buf, buf_avail, read_bytes);
}

bool PipeFile::readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes)
{
	return readIntoBuffer(hStderr, buf, buf_avail, read_bytes);
}

bool PipeFile::readIntoBuffer(HANDLE hStd, char* buf, size_t buf_avail, size_t& read_bytes)
{
	DWORD dwread = 0;
	BOOL b = ReadFile(hStd, buf, static_cast<DWORD>(buf_avail), &dwread, NULL);

	read_bytes = static_cast<size_t>(dwread);

	if(b)
	{
		return true;
	}
	else
	{
		return false;
	}
}
