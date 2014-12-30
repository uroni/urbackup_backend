#include "PipeFile.h"
#include <assert.h>
#include <stdexcept>
#include <memory.h>
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

const size_t buffer_size = 5*1024*1024;
const _u32 buffer_keep_free = 1*1024*1024;

void PipeFile::init()
{
	last_read = Server->getTimeMS();

	buffer_mutex.reset(Server->createMutex());

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
			readBuf(&ret[0], tr);
		}		
		if(getReadAvail()==0)
		{
			stream_size=curr_pos;
		}
		return ret;
	}
	else
	{
		std::string ret;
		readBuf(&ret[0], tr);
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
		readBuf(buffer, tr);
		if(getReadAvail()==0)
		{
			stream_size=curr_pos;
		}
		return static_cast<_u32>(tr);
	}
	else
	{
		readBuf(buffer, bsize);
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
			&& static_cast<size_t>(seeked_r_pos)<=buf_w_pos)
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
		if(seeked_r_pos >= static_cast<_i64>(buf_w_reserved_pos) &&
			seeked_r_pos < static_cast<_i64>(buffer_size))
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
	return stream_size;
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
			Server->wait(10);
			return true;
		}

		if(buf_r_pos-buf_w_pos<bsize_free)
		{
			bsize_free = buf_r_pos - buf_w_pos;
		}
	}

	if(bsize_free==0 && buf_w_pos>buf_r_pos)
	{
		if(buf_r_pos<buffer_keep_free)
		{
			lock.relock(NULL);
			Server->wait(10);
			return true;
		}

		buf_circle=true;
		buf_w_pos = 0;
		buf_w_reserved_pos=0;

		bsize_free = buf_r_pos;
	}

	if(bsize_free==0)
	{
		lock.relock(NULL);
		Server->wait(10);
		return true;
	}

	buf_w_reserved_pos = buf_w_pos + bsize_free;

	size_t read = 0;

	lock.relock(NULL);

	bool b = readStdoutIntoBuffer(&buffer[buf_w_pos], bsize_free, read);

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

	size_t read = 0;
	bool b = readStderrIntoBuffer(buf, 4096, read);

	if(!b)
	{
		return false;
	}
	else if(read>0)
	{
		IScopedLock lock(buffer_mutex.get());

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

void PipeFile::readBuf(char* buf, size_t toread)
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
			if(cread>0)
			{
				memcpy(buf, &buffer[buf_r_pos], cread);
			}			
			buf_r_pos = 0;
			curr_pos+=cread;

			readBuf(buf + cread, toread - cread);
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

std::string PipeFile::getStdErr()
{
	IScopedLock lock(buffer_mutex.get());
	return stderr_ret;
}
