#include "PipeFile.h"
#include <assert.h>
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

const size_t buffer_size = 5*1024*1024;
const _u32 buffer_keep_free = 1*1024*1024;

PipeFile::PipeFile(const std::wstring& pCmd)
	: curr_pos(0), has_error(false), cmd(pCmd),
	hStderr(INVALID_HANDLE_VALUE),
	hStdout(INVALID_HANDLE_VALUE), buf_w_pos(0), buf_r_pos(0), buf_w_reserved_pos(0),
	threadidx(0), has_eof(false), has_read_eof(false),
	buf_circle(false)
{
	last_read = Server->getTimeMS();

	buffer_mutex.reset(Server->createMutex());

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

	if(!has_error)
	{
		buffer.resize(buffer_size);
		Server->getThreadPool()->execute(this);
		Server->getThreadPool()->execute(this);
	}
}

std::string PipeFile::Read(_u32 tr, bool *has_error/*=NULL*/)
{
	if(tr==0)
	{
		return std::string();
	}

	IScopedLock lock(buffer_mutex.get());

	last_read = Server->getTimeMS();

	while(getReadAvail()<tr && !has_eof)
	{
		lock.relock(NULL);
		Server->wait(10);
		lock.relock(buffer_mutex.get());
	}

	if(has_eof)
	{
		tr = (std::min)(static_cast<_u32>(getReadAvail()), tr);
		std::string ret;
		if(tr>0)
		{
			ret.resize(tr);
			read(&ret[0], tr);
		}		
		if(getReadAvail()==0)
		{
			has_read_eof=true;
		}
		return ret;
	}
	else
	{
		std::string ret;
		read(&ret[0], tr);
		return ret;
	}
}

_u32 PipeFile::Read(char* buffer, _u32 bsize, bool *has_error/*=NULL*/)
{
	IScopedLock lock(buffer_mutex.get());

	last_read = Server->getTimeMS();

	while(getReadAvail()<(std::max)(bsize, buffer_keep_free) && !has_eof)
	{
		lock.relock(NULL);
		Server->wait(10);
		lock.relock(buffer_mutex.get());
	}

	if(has_eof)
	{
		size_t tr = (std::min)(getReadAvail(), static_cast<size_t>(bsize));
		read(buffer, tr);
		if(getReadAvail()==0)
		{
			has_read_eof=true;
		}
		return static_cast<_u32>(tr);
	}
	else
	{
		read(buffer, bsize);
		return bsize;
	}
}

_u32 PipeFile::Write(const std::string &tw, bool *has_error/*=NULL*/)
{
	throw std::logic_error("The method or operation is not implemented.");
}

_u32 PipeFile::Write(const char* buffer, _u32 bsiz, bool *has_error/*=NULL*/)
{
	throw std::logic_error("The method or operation is not implemented.");
}

bool PipeFile::Seek(_i64 spos)
{
	_i64 seek_off = spos - curr_pos;

	IScopedLock lock(buffer_mutex.get());

	_i64 seeked_r_pos = static_cast<_i64>(buf_r_pos)+seek_off;

	if(buf_r_pos<=buf_w_pos)
	{
		if(seeked_r_pos>=0
			&& static_cast<size_t>(seeked_r_pos)<buf_w_pos)
		{
			curr_pos = spos;
			buf_r_pos=static_cast<size_t>(seeked_r_pos);
			return true;
		}
		else if(seeked_r_pos<0 &&
			buffer_size-buf_w_reserved_pos>static_cast<size_t>(-1*seeked_r_pos) &&
			buf_circle)
		{
			buf_r_pos = buffer_size + seeked_r_pos;
			curr_pos = spos;
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		if(seeked_r_pos>static_cast<_i64>(buf_w_reserved_pos) &&
			seeked_r_pos<static_cast<_i64>(buffer_size))
		{
			buf_r_pos=static_cast<size_t>(seeked_r_pos);
			curr_pos = spos;
			return true;
		}
		else if(seeked_r_pos>=buffer_size &&
			seeked_r_pos-buffer_size<=buf_w_pos)
		{
			buf_r_pos=static_cast<size_t>(seeked_r_pos - buffer_size);
			curr_pos = spos;
			return true;
		}
		else
		{
			return false;
		}
	}
}

_i64 PipeFile::Size(void)
{
	IScopedLock lock(buffer_mutex.get());
	if(!has_read_eof)
	{
		return -1;
	}
	else
	{
		return curr_pos;
	}
}

_i64 PipeFile::RealSize()
{
	return Size();
}

std::string PipeFile::getFilename(void)
{
	return Server->ConvertToUTF8(cmd);
}

std::wstring PipeFile::getFilenameW(void)
{
	return cmd;
}

PipeFile::~PipeFile()
{
	CloseHandle(hStdout);
	CloseHandle(hStderr);
}

void PipeFile::operator()()
{
	IScopedLock lock(buffer_mutex.get());

	if(threadidx==0)
	{
		++threadidx;
		lock.relock(NULL);

		while(fillBuffer())
		{

		}
	}
	else
	{
		lock.relock(NULL);

		while(readStderr())
		{

		}
	}
}

bool PipeFile::fillBuffer()
{
	IScopedLock lock(buffer_mutex.get());

	size_t bsize_free = buffer_size - buf_w_pos;

	if(buf_r_pos>buf_w_pos)
	{
		if(buf_r_pos-buf_w_pos<buffer_keep_free)
		{
			lock.relock(NULL);
			Sleep(10);
			return true;
		}

		if(buf_r_pos-buf_w_pos<bsize_free)
		{
			bsize_free = buf_r_pos - buf_w_pos;
		}
	}

	if(bsize_free==0 && buf_w_pos>buf_r_pos)
	{
		buf_circle=true;
		buf_w_pos = 0;
		buf_w_reserved_pos=0;

		bsize_free = buf_r_pos;
	}

	if(bsize_free==0)
	{
		lock.relock(NULL);
		Sleep(10);
		return true;
	}

	buf_w_reserved_pos = buf_w_pos + bsize_free;

	DWORD read = 0;

	lock.relock(NULL);

	BOOL b = ReadFile(hStdout, &buffer[buf_w_pos], static_cast<DWORD>(bsize_free), &read, NULL);

	lock.relock(buffer_mutex.get());

	buf_w_pos+=read;
	buf_w_reserved_pos=buf_w_pos;

	if(!b)
	{
		has_eof=true;
		return false;
	}
	else
	{
		return true;
	}
}

bool PipeFile::readStderr()
{
	char buf[4096];

	DWORD read = 0;
	BOOL b = ReadFile(hStderr, buf, 4096, &read, NULL);

	if(!b)
	{
		return false;
	}
	else if(read>0)
	{
		size_t stderr_pos = stderr_ret.size();
		stderr_ret.resize(stderr_pos + read);
		memcpy(&stderr_ret[stderr_pos], buf, read);
	}

	return true;
}

size_t PipeFile::getReadAvail()
{
	if(buf_w_pos>=buf_r_pos)
	{
		return buf_w_pos-buf_r_pos;
	}
	else
	{
		return buffer_size - buf_r_pos + buf_w_pos;
	}
}

void PipeFile::read(char* buf, size_t toread)
{
	if(buf_w_pos>=buf_r_pos)
	{
		assert(buf_w_pos - buf_r_pos >= toread);
		char* ptr_str = buffer.data();
		memcpy(buf, &buffer[buf_r_pos], toread);
		buf_r_pos+=toread;
		curr_pos+=toread;
	}
	else
	{
		if(toread <= buffer_size - buf_r_pos)
		{
			memcpy(buf, &buffer[buf_r_pos], toread);
			buf_r_pos += toread;
			curr_pos+=toread;
		}
		else
		{
			size_t cread = buffer_size - buf_r_pos;
			memcpy(buf, &buffer[buf_r_pos], cread);
			buf_r_pos = 0;
			curr_pos+=cread;

			read(buf + cread, toread - cread);
		}		
	}
}

int64 PipeFile::getLastRead()
{
	return last_read;
}

bool PipeFile::getHasError()
{
	return has_error;
}


