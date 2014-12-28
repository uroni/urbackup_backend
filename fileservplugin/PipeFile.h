#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include <Windows.h>
#include "../Interface/Mutex.h"
#include <memory>


class PipeFile : public IFile, public IThread
{
public:
	PipeFile(const std::wstring& pCmd);
	~PipeFile();

	virtual void operator()();

	virtual std::string Read(_u32 tr, bool *has_error=NULL);

	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error=NULL);

	virtual _u32 Write(const std::string &tw, bool *has_error=NULL);

	virtual _u32 Write(const char* buffer, _u32 bsiz, bool *has_error=NULL);

	virtual bool Seek(_i64 spos);

	virtual _i64 Size(void);

	virtual _i64 RealSize();

	virtual std::string getFilename(void);

	virtual std::wstring getFilenameW(void);

	int64 getLastRead();

	bool getHasError();

private:

	bool fillBuffer();
	bool readStderr();

	size_t getReadAvail();
	void read(char* buf, size_t toread);

	bool has_error;
	std::wstring cmd;

	int64 curr_pos;
	size_t buf_w_pos;
	size_t buf_w_reserved_pos;
	size_t buf_r_pos;
	bool buf_circle;
	std::vector<char> buffer;
	std::string stderr_ret;
	std::auto_ptr<IMutex> buffer_mutex;
	size_t threadidx;
	bool has_eof;
	int64 stream_size;

	HANDLE hFile;
	HANDLE hStdout;
	HANDLE hStderr;
	PROCESS_INFORMATION proc_info;

	int64 last_read;
};