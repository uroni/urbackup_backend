#pragma once

class IPipeFileExt
{
public:
	virtual bool getExitCode(int & exit_code) = 0;
	virtual void forceExit() = 0;
	virtual bool readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes) = 0;
	virtual void finishStdout() = 0;
	virtual bool readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes) = 0;
};