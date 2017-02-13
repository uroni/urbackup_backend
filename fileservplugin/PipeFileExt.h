#pragma once
#include "PipeFileBase.h"
#include "IPipeFileExt.h"
#include <memory>

class PipeFileExt : public PipeFileBase
{
public:
	PipeFileExt(IPipeFileExt* file_ext, std::string fn);

	virtual bool getExitCode(int & exit_code);

	virtual void forceExitWait();
protected:
	virtual bool readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes);
	virtual void finishStdout();
	virtual bool readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes);
private:
	std::auto_ptr<IPipeFileExt> file_ext;
};