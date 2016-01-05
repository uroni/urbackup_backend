/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#include "PipeFileBase.h"
#include <assert.h>
#include <stdexcept>
#include <memory.h>
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"

const size_t buffer_size = 5*1024*1024;
const _u32 buffer_keep_free = 1*1024*1024;


PipeFileBase::PipeFileBase(const std::string& pCmd)
	: curr_pos(0), has_error(false), cmd(pCmd), buf_w_pos(0), buf_r_pos(0), buf_w_reserved_pos(0),
	threadidx(0), has_eof(false), stream_size(-1),
	buf_circle(false), stdout_thread(ILLEGAL_THREADPOOL_TICKET), stderr_thread(ILLEGAL_THREADPOOL_TICKET)
{
	last_read = Server->getTimeMS();

	buffer_mutex.reset(Server->createMutex());
}


void PipeFileBase::init()
{
	if(!has_error)
	{
		sha_def_init(&sha_ctx);

		buffer.resize(buffer_size);
		stdout_thread = Server->getThreadPool()->execute(this);
		stderr_thread = Server->getThreadPool()->execute(this);
	}
}


std::string PipeFileBase::Read(_u32 tr, bool *has_error/*=NULL*/)
{
	if(tr==0)
	{
		return std::string();
	}

	IScopedLock lock(buffer_mutex.get());

	last_read = Server->getTimeMS();

	int64 starttime = Server->getTimeMS();
	size_t read_avail = getReadAvail();
	while(read_avail<tr && !has_eof && (read_avail==0 || Server->getTimeMS()-starttime<60000))
	{
		lock.relock(NULL);
		Server->wait(100);
		lock.relock(buffer_mutex.get());
		read_avail = getReadAvail();
	}

	if(has_eof)
	{
		tr = (std::min)(static_cast<_u32>(read_avail), tr);
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
		tr = (std::min)(static_cast<_u32>(read_avail), tr);

		std::string ret;
		ret.resize(tr);
		readBuf(&ret[0], tr);
		return ret;
	}
}

_u32 PipeFileBase::Read(char* buffer, _u32 bsize, bool *has_error/*=NULL*/)
{
	IScopedLock lock(buffer_mutex.get());

	last_read = Server->getTimeMS();

	int64 starttime = Server->getTimeMS();
	size_t read_avail = getReadAvail();
	while(read_avail<bsize && !has_eof && (read_avail==0 || Server->getTimeMS()-starttime<60000))
	{
		lock.relock(NULL);
		Server->wait(100);
		lock.relock(buffer_mutex.get());
		read_avail = getReadAvail();
	}

	if(has_eof)
	{
		size_t tr = (std::min)(read_avail, static_cast<size_t>(bsize));
		readBuf(buffer, tr);
		if(getReadAvail()==0)
		{
			stream_size=curr_pos;
		}
		return static_cast<_u32>(tr);
	}
	else
	{
		size_t tr = (std::min)(read_avail, static_cast<size_t>(bsize));
		readBuf(buffer, tr);
		return static_cast<_u32>(tr);
	}
}

_u32 PipeFileBase::Write(const std::string &tw, bool *has_error/*=NULL*/)
{
	throw std::logic_error("The method or operation is not implemented.");
}

_u32 PipeFileBase::Write(const char* buffer, _u32 bsiz, bool *has_error/*=NULL*/)
{
	throw std::logic_error("The method or operation is not implemented.");
}

bool PipeFileBase::Seek(_i64 spos)
{
	_i64 seek_off = spos - curr_pos;
	
	if(seek_off==0)
	{
		return true;
	}

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

_i64 PipeFileBase::Size(void)
{
	IScopedLock lock(buffer_mutex.get());
	return stream_size;
}

_i64 PipeFileBase::RealSize()
{
	return Size();
}

std::string PipeFileBase::getFilename(void)
{
	return cmd;
}

void PipeFileBase::operator()()
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

bool PipeFileBase::fillBuffer()
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

	if(bsize_free==0 && buf_w_pos>=buf_r_pos)
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

	sha_def_update(&sha_ctx, reinterpret_cast<const unsigned char*>(&buffer[buf_w_pos]), static_cast<unsigned int>(read));

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

bool PipeFileBase::readStderr()
{
	char buf[4096];

	size_t read = 0;
	bool b = readStderrIntoBuffer(buf, 4096, read);

	if(!b)
	{
		int64 starttime = Server->getTimeMS();
		IScopedLock lock(buffer_mutex.get());

		while(!has_eof && Server->getTimeMS()-starttime<10000)
		{
			lock.relock(NULL);
			Server->wait(100);
			lock.relock(buffer_mutex.get());
		}

		if(has_eof)
		{
			unsigned char dig[SHA_DEF_DIGEST_SIZE];
			sha_def_final(&sha_ctx, dig);

			int64 timems = little_endian(Server->getTimeMS());

			stderr_ret.append(1, 1);

			stderr_ret.append(reinterpret_cast<const char*>(dig),
				reinterpret_cast<const char*>(dig)+SHA_DEF_DIGEST_SIZE);
		}

		return false;
	}
	else if(read>0)
	{
		IScopedLock lock(buffer_mutex.get());


		int64 timems = little_endian(Server->getTimeMS());
		unsigned int le_read = little_endian(static_cast<unsigned int>(read));

		stderr_ret.append(1, 0);

		stderr_ret.append(reinterpret_cast<const char*>(&timems),
			reinterpret_cast<const char*>(&timems)+sizeof(timems));

		stderr_ret.append(reinterpret_cast<const char*>(&le_read),
			reinterpret_cast<const char*>(&le_read)+sizeof(le_read));

		stderr_ret.append(buf, buf+read);
	}

	return true;
}

size_t PipeFileBase::getReadAvail()
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

void PipeFileBase::readBuf(char* buf, size_t toread)
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

int64 PipeFileBase::getLastRead()
{
	return last_read;
}

bool PipeFileBase::getHasError()
{
	return has_error;
}

std::string PipeFileBase::getStdErr()
{
	IScopedLock lock(buffer_mutex.get());
	return stderr_ret;
}

void PipeFileBase::waitForExit()
{
	std::vector<THREADPOOL_TICKET> tickets;
	tickets.push_back(stdout_thread);
	tickets.push_back(stderr_thread);

	Server->getThreadPool()->waitFor(tickets);
}

bool PipeFileBase::PunchHole( _i64 spos, _i64 size )
{
	return false;
}

bool PipeFileBase::Sync()
{
	return false;
}
