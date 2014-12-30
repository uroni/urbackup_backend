#include "PipeFile.h"

PipeFile::PipeFile(const std::wstring& pCmd)
	: curr_pos(0), has_error(false), cmd(pCmd),
	hStderr(INVALID_HANDLE_VALUE),
	hStdout(INVALID_HANDLE_VALUE), buf_w_pos(0), buf_r_pos(0), buf_w_reserved_pos(0),
	threadidx(0), has_eof(false), stream_size(-1),
	buf_circle(false)
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

	BOOL b = CreateProcessW(NULL, const_cast<LPWSTR>(cmd.c_str()),
		&saAttr, NULL, TRUE, 0, NULL, NULL, &start_info,
		&proc_info);

	if(!b)
	{
		Server->Log(L"Error starting script \"" + cmd + L"\"", LL_ERROR);
		has_error=true;
	}

	CloseHandle(hStderrW);
	CloseHandle(hStdoutW);

	init();
}

PipeFile::~PipeFile()
{
	CloseHandle(hStdout);
	CloseHandle(hStderr);

	TerminateProcess(proc_info.hProcess, 0);
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

bool PipeFile::readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read)
{
	return readIntoBuffer(hStdout, buf, buf_avail, read);
}

bool PipeFile::readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read)
{
	return readIntoBuffer(hStderr, buf, buf_avail, read);
}

bool PipeFile::readIntoBuffer(HANDLE hStd, char* buf, size_t buf_avail, size_t& read)
{
	DWORD dwread = 0;
	BOOL b = ReadFile(hStd, buf, static_cast<DWORD>(buf_avail), &dwread, NULL);

	read = static_cast<size_t>(dwread);

	if(b)
	{
		return true;
	}
	else
	{
		return false;
	}
}
