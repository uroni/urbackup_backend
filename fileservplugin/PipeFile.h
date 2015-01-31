#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Pipe.h"
#include "PipeFileBase.h"
#include <memory>

#ifdef _WIN32
#include <Windows.h>
#endif


class PipeFile : public PipeFileBase
{
public:
	PipeFile(const std::wstring& pCmd);
	~PipeFile();

	virtual bool getExitCode(int& exit_code);

protected:

	virtual bool readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes);
	virtual bool readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes);
#ifdef _WIN32
	bool readIntoBuffer(HANDLE hStd, char* buf, size_t buf_avail, size_t& read_bytes);
#else
	bool readIntoBuffer(int hStd, char* buf, size_t buf_avail, size_t& read_bytes);
#endif

#ifdef _WIN32
	HANDLE hStdout;
	HANDLE hStderr;
	PROCESS_INFORMATION proc_info;
#else
	int hStdout;
	int hStderr;
	pid_t child_pid;
#endif
};
