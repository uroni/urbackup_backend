/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "CompressedPipe2.h"
#include "../Interface/Server.h"
#include <limits.h>
#include <memory.h>
#include <string.h>
#include "../stringtools.h"
#include <assert.h>


const size_t max_send_size=20000;
const size_t output_incr_size=8192;

CompressedPipe2::CompressedPipe2(IPipe *cs, int compression_level)
	: cs(cs), has_error(false),
	uncompressed_sent_bytes(0), uncompressed_received_bytes(0), sent_flushes(0)
{
	decomp_buffer_pos=0;
	decomp_read_pos=0;
	comp_buffer.resize(4096);
	destroy_cs=false;

	memset(&inf_stream, 0, sizeof(mz_stream));
	memset(&def_stream, 0, sizeof(mz_stream));

	if(mz_deflateInit(&def_stream, compression_level)!=MZ_OK)
	{
		throw new std::runtime_error("Error initializing compression stream");
	}
	if(mz_inflateInit(&inf_stream)!=MZ_OK)
	{
		throw new std::runtime_error("Error initializing decompression stream");
	}
}

CompressedPipe2::~CompressedPipe2(void)
{
	mz_deflateEnd(&def_stream);
	mz_inflateEnd(&inf_stream);

	if(destroy_cs)
	{
		Server->destroy(cs);
	}
}

size_t CompressedPipe2::ReadToBuffer(char *buffer, size_t bsize)
{
	if(decomp_read_pos<decomp_buffer_pos)
	{
		size_t toread=(std::min)(bsize, decomp_buffer_pos-decomp_read_pos);
		memcpy(buffer, &decomp_buffer[decomp_read_pos], toread);
		decomp_read_pos+=toread;
		uncompressed_received_bytes+=toread;

		if(decomp_read_pos==decomp_buffer_pos)
		{
			decomp_read_pos=0;
			decomp_buffer_pos=0;
		}

		return toread;
	}
	return 0;
}

size_t CompressedPipe2::ReadToString(std::string *ret)
{
	if(decomp_read_pos<decomp_buffer_pos)
	{
		size_t toread=decomp_buffer_pos-decomp_read_pos;
		ret->resize(toread);
		uncompressed_received_bytes+=toread;
		memcpy((char*)ret->c_str(), &decomp_buffer[decomp_read_pos], toread);
		decomp_read_pos=0;
		decomp_buffer_pos=0;
		return toread;
	}
	return 0;
}

size_t CompressedPipe2::Read(char *buffer, size_t bsize, int timeoutms)
{
	size_t rc=ReadToBuffer(buffer, bsize);
	if(rc>0) return rc;

	if(timeoutms==0)
	{
		rc=cs->Read(buffer, bsize, timeoutms);
		Process(buffer, rc);
		if(has_error)
		{
			return 0;
		}
		return ReadToBuffer(buffer, bsize);
	}
	else if(timeoutms==-1)
	{
		do
		{
			rc=cs->Read(buffer, bsize, timeoutms);
			if(rc==0)
				return 0;

			Process(buffer, rc);
			if(has_error)
			{
				return 0;
			}
			rc=ReadToBuffer(buffer, bsize);
		}
		while(rc==0);
		return rc;
	}

	int64 starttime=Server->getTimeMS();
	do
	{
		int left=timeoutms-static_cast<int>(Server->getTimeMS()-starttime);

		rc=cs->Read(buffer, bsize, left);
		if(rc==0)
			return 0;
		Process(buffer, rc);
		if(has_error)
		{
			return 0;
		}
		rc=ReadToBuffer(buffer, bsize);
	}
	while(rc==0 && Server->getTimeMS()-starttime<static_cast<int64>(timeoutms));

	return rc;
}

void CompressedPipe2::Process(const char *buffer, size_t bsize)
{
	inf_stream.avail_in = static_cast<unsigned int>(bsize);
	inf_stream.next_in = reinterpret_cast<const unsigned char*>(buffer);

	do 
	{
		if(decomp_buffer_pos==decomp_buffer.size())
		{
			decomp_buffer.resize(decomp_buffer.size()+output_incr_size);
		}

		inf_stream.next_out=reinterpret_cast<unsigned char*>(&decomp_buffer[decomp_buffer_pos]);
		inf_stream.avail_out=static_cast<unsigned int>(decomp_buffer.size()-decomp_buffer_pos);

		int rc = mz_inflate(&inf_stream, MZ_SYNC_FLUSH);

		size_t used = (decomp_buffer.size()-decomp_buffer_pos) - inf_stream.avail_out;

		decomp_buffer_pos+=used;

		if(rc!=MZ_OK && rc!=MZ_STREAM_END)
		{
			Server->Log("Error decompressing stream: "+nconvert(rc));
			has_error=true;
			return;
		}
	} while (inf_stream.avail_out==0);
}


void CompressedPipe2::ProcessToString( const char *buffer, size_t bsize, std::string* ret )
{
	assert(decomp_buffer_pos==0);

	inf_stream.avail_in = static_cast<unsigned int>(bsize);
	inf_stream.next_in = reinterpret_cast<const unsigned char*>(buffer);

	size_t data_pos = 0;

	do 
	{
		if(data_pos==ret->size())
		{
			ret->resize(ret->size()+output_incr_size);
		}

		inf_stream.next_out=reinterpret_cast<unsigned char*>(&(*ret)[data_pos]);
		inf_stream.avail_out=static_cast<unsigned int>(ret->size()-data_pos);

		int rc = mz_inflate(&inf_stream, MZ_SYNC_FLUSH);

		size_t used = (ret->size()-data_pos) - inf_stream.avail_out;

		data_pos+=used;

		if(rc!=MZ_OK && rc!=MZ_STREAM_END)
		{
			Server->Log("Error decompressing stream: "+nconvert(rc));
			has_error=true;
			return;
		}

		if(inf_stream.avail_out!=0)
		{
			ret->resize(ret->size()-inf_stream.avail_out);
		}

	} while (inf_stream.avail_out==0);
}

bool CompressedPipe2::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	const char *ptr=buffer;
	size_t cbsize=bsize;
	int64 starttime = Server->getTimeMS();
	do
	{
		cbsize=(std::min)(max_send_size, bsize);

		bsize-=cbsize;
		uncompressed_sent_bytes=cbsize;

		bool has_next = bsize>0;
		bool curr_flush = has_next ? false : flush;

		if(curr_flush)
		{
			++sent_flushes;
		}

		
		def_stream.avail_in = static_cast<unsigned int>(cbsize);
		def_stream.next_in = reinterpret_cast<const unsigned char*>(ptr);

		do 
		{
			def_stream.avail_out = static_cast<unsigned int>(comp_buffer.size());
			def_stream.next_out = reinterpret_cast<unsigned char*>(&comp_buffer[0]);

			int rc = mz_deflate(&def_stream, curr_flush ? MZ_SYNC_FLUSH : MZ_NO_FLUSH);

			if(rc!=MZ_OK && rc!=MZ_STREAM_END)
			{
				Server->Log("Error compressing stream: "+nconvert(rc));
				has_error=true;
				return false;
			}

			size_t used = comp_buffer.size() - def_stream.avail_out;

			int curr_timeout = timeoutms;

			if(curr_timeout>0)
			{
				int64 time_elapsed = Server->getTimeMS()-starttime;
				if(time_elapsed>curr_timeout)
				{
					return false;
				}
				else
				{
					curr_timeout-=static_cast<int>(time_elapsed);
				}
			}

			if(used>0)
			{
				bool b=cs->Write(&comp_buffer[0], used, curr_timeout, curr_flush);
				if(!b)
					return false;
			}
			else if(!has_next && flush)
			{
				return cs->Flush(curr_timeout);
			}

		} while(def_stream.avail_out==0);

		ptr+=cbsize;
		
	} while(bsize>0);

	return true;
}

size_t CompressedPipe2::Read(std::string *ret, int timeoutms)
{
	size_t rc=ReadToString(ret);
	if(rc>0) return rc;

	if(timeoutms==0)
	{
		std::string tmp;
		rc=cs->Read(&tmp, timeoutms);
		ProcessToString(tmp.c_str(), tmp.size(), ret);
		if(has_error)
		{
			return 0;
		}
		return ret->size();
	}
	else if(timeoutms==-1)
	{
		do
		{
			std::string tmp;
			rc=cs->Read(&tmp, timeoutms);
			if(rc==0)
				return 0;

			ProcessToString(tmp.c_str(), tmp.size(), ret);
			if(has_error)
			{
				return 0;
			}
			rc=ret->size();
		}
		while(rc==0);
		return rc;
	}

	int64 starttime=Server->getTimeMS();
	do
	{
		int left=timeoutms-static_cast<int>(Server->getTimeMS()-starttime);

		std::string tmp;
		rc=cs->Read(&tmp, left);
		if(rc==0)
			return 0;

		ProcessToString(tmp.c_str(), tmp.size(), ret);
		if(has_error)
		{
			return 0;
		}
		rc=ret->size();
	}
	while(rc==0 && Server->getTimeMS()-starttime<static_cast<int64>(timeoutms));

	return rc;
}

bool CompressedPipe2::Write(const std::string &str, int timeoutms, bool flush)
{
	return Write(str.c_str(), str.size(), timeoutms, flush);
}

/**
* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
*/
bool CompressedPipe2::isWritable(int timeoutms)
{
	return cs->isWritable(timeoutms);
}

bool CompressedPipe2::isReadable(int timeoutms)
{
	if(decomp_read_pos<decomp_buffer_pos)
		return true;
	else
		return cs->isReadable(timeoutms);
}

bool CompressedPipe2::hasError(void)
{
	return cs->hasError() || has_error;
}

void CompressedPipe2::shutdown(void)
{
	cs->shutdown();
}

size_t CompressedPipe2::getNumElements(void)
{
	return cs->getNumElements();
}

void CompressedPipe2::destroyBackendPipeOnDelete(bool b)
{
	destroy_cs=b;
}

IPipe *CompressedPipe2::getRealPipe(void)
{
	return cs;
}

void CompressedPipe2::addThrottler(IPipeThrottler *throttler)
{
	cs->addThrottler(throttler);
}

void CompressedPipe2::addOutgoingThrottler(IPipeThrottler *throttler)
{
	cs->addOutgoingThrottler(throttler);
}

void CompressedPipe2::addIncomingThrottler(IPipeThrottler *throttler)
{
	cs->addIncomingThrottler(throttler);
}

_i64 CompressedPipe2::getTransferedBytes(void)
{
	return cs->getTransferedBytes();
}

void CompressedPipe2::resetTransferedBytes(void)
{
	cs->resetTransferedBytes();
}

bool CompressedPipe2::Flush( int timeoutms/*=-1 */ )
{
	return Write(NULL, 0, timeoutms, true);
}

int64 CompressedPipe2::getUncompressedReceivedBytes()
{
	return uncompressed_received_bytes;
}

int64 CompressedPipe2::getUncompressedSentBytes()
{
	return uncompressed_sent_bytes;
}

int64 CompressedPipe2::getSentFlushes()
{
	return sent_flushes;
}

_i64 CompressedPipe2::getRealTransferredBytes()
{
	return getUncompressedSentBytes()+getUncompressedReceivedBytes();
}
