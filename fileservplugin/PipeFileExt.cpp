#include "PipeFileExt.h"

PipeFileExt::PipeFileExt(IPipeFileExt * file_ext, std::string fn)
	: file_ext(file_ext), PipeFileBase(fn)
{
}

bool PipeFileExt::readStdoutIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes)
{
	return file_ext->readStdoutIntoBuffer(buf, buf_avail, read_bytes);
}

void PipeFileExt::finishStdout()
{
	file_ext->finishStdout();
}

bool PipeFileExt::readStderrIntoBuffer(char * buf, size_t buf_avail, size_t & read_bytes)
{
	return file_ext->readStderrIntoBuffer(buf, buf_avail, read_bytes);
}

bool PipeFileExt::getExitCode(int & exit_code)
{
	return file_ext->getExitCode(exit_code);
}

void PipeFileExt::forceExitWait()
{
	file_ext->forceExit();
	waitForExit();
}
