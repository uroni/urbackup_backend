#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../Interface/Mutex.h"
#include "../Interface/Pipe.h"
#include <memory>

class PipeFileBase : public IFile, public IThread
{
public:
	PipeFileBase(const std::string& pCmd);

	virtual void operator()();

	virtual std::string Read(_u32 tr, bool *has_error=NULL);

	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error=NULL);

	virtual _u32 Write(const std::string &tw, bool *has_error=NULL);

	virtual _u32 Write(const char* buffer, _u32 bsiz, bool *has_error=NULL);

	virtual bool Seek(_i64 spos);

	virtual _i64 Size(void);

	virtual _i64 RealSize();

	virtual std::string getFilename(void);

	int64 getLastRead();

	bool getHasError();

	virtual bool getExitCode(int& exit_code) = 0;

	std::string getStdErr();

	virtual void forceExitWait() = 0;

	virtual bool PunchHole( _i64 spos, _i64 size );

	virtual bool Sync();

protected:

	void init();

	virtual bool readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes) = 0;
	virtual bool readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes) = 0;

	bool has_error;

	void waitForExit();

private:
	bool fillBuffer();
	bool readStderr();

	size_t getReadAvail();
	void readBuf(char* buf, size_t toread);


	std::string cmd;

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

	int64 last_read;

	THREADPOOL_TICKET stdout_thread;
	THREADPOOL_TICKET stderr_thread;
};
