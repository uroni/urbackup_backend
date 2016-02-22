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

#include "CompressedPipe2.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include <limits.h>
#include <memory.h>
#include <string.h>
#include "../stringtools.h"
#include <assert.h>
#include <stdexcept>
#include <assert.h>
#include "InternetServicePipe2.h"


const size_t max_send_size=20000;
const size_t output_incr_size=8192;
const size_t output_max_size=32*1024;

CompressedPipe2::CompressedPipe2(IPipe *cs, int compression_level)
	: cs(cs), has_error(false),
	uncompressed_sent_bytes(0), uncompressed_received_bytes(0), sent_flushes(0),
	input_buffer_size(0), read_mutex(Server->createMutex()), write_mutex(Server->createMutex())
{
	comp_buffer.resize(4096);
	input_buffer.resize(16384);
	destroy_cs=false;

	memset(&inf_stream, 0, sizeof(mz_stream));
	memset(&def_stream, 0, sizeof(mz_stream));

	if(mz_deflateInit(&def_stream, compression_level)!=MZ_OK)
	{
		throw std::runtime_error("Error initializing compression stream");
	}
	if(mz_inflateInit(&inf_stream)!=MZ_OK)
	{
		throw std::runtime_error("Error initializing decompression stream");
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

size_t CompressedPipe2::Read(char *buffer, size_t bsize, int timeoutms)
{
	IScopedLock lock(read_mutex.get());

	if(input_buffer_size>0)
	{
		size_t rc = ProcessToBuffer(buffer, bsize, true);
		if(rc>0)
		{
			return rc;
		}
		else if(input_buffer_size==input_buffer.size())
		{
			input_buffer.resize(input_buffer.size()+output_incr_size);
		}
	}

	if(timeoutms==0)
	{
		size_t rc=cs->Read(input_buffer.data()+input_buffer_size, input_buffer.size()-input_buffer_size, timeoutms);
		if(rc==0)
			return 0;

		input_buffer_size+=rc;		
		return ProcessToBuffer(buffer, bsize, false);
	}
	else if(timeoutms==-1)
	{
		size_t rc;
		do
		{
			rc=cs->Read(input_buffer.data()+input_buffer_size, input_buffer.size()-input_buffer_size, timeoutms);
			if(rc==0)
				return 0;
			if(has_error)
			{
				return 0;
			}

			input_buffer_size += rc;	
			rc = ProcessToBuffer(buffer, bsize, false);
		}
		while(rc==0);
		return rc;
	}

	int64 starttime=Server->getTimeMS();
	size_t rc;
	do
	{
		int left=timeoutms-static_cast<int>(Server->getTimeMS()-starttime);
		if (left < 0)
		{
			break;
		}

		rc=cs->Read(input_buffer.data()+input_buffer_size, input_buffer.size()-input_buffer_size, left);
		if(rc==0)
			return 0;
		if(has_error)
		{
			return 0;
		}
		input_buffer_size += rc;	
		rc = ProcessToBuffer(buffer, bsize, false);
	}
	while(rc==0 && Server->getTimeMS()-starttime<static_cast<int64>(timeoutms));

	return rc;
}

size_t CompressedPipe2::ProcessToBuffer(char *buffer, size_t bsize, bool fromLast)
{
	bool set_out=false;
	if(fromLast)
	{
		inf_stream.next_out=reinterpret_cast<unsigned char*>(buffer);
		inf_stream.avail_out=static_cast<unsigned int>(bsize);
		set_out=true;

		Server->Log("mz_inflate(1) avail_in=" + convert(inf_stream.avail_in) + " avail_out=" + convert(inf_stream.avail_out), LL_DEBUG);
		int rc = mz_inflate(&inf_stream, MZ_PARTIAL_FLUSH);

		assert(bsize >= inf_stream.avail_out);
		size_t used = bsize - inf_stream.avail_out;
		uncompressed_received_bytes+=used;

		Server->Log("rc=" + convert(rc) + " used=" + convert(used) + " avail_in = " + convert(inf_stream.avail_in) + " avail_out = " + convert(inf_stream.avail_out), LL_DEBUG);

		if(rc!=MZ_OK && rc!=MZ_STREAM_END && rc!=MZ_BUF_ERROR /*Needs more input*/)
		{
			Server->Log("Error decompressing stream(1): "+convert(rc));
			has_error=true;
			return 0;
		}

		if(inf_stream.avail_in==0 && inf_stream.avail_out!=0)
		{
			input_buffer_size=0;
		}
		else if(inf_stream.avail_in==0)
		{
			inf_stream.next_in=NULL;
		}

		return used;
	}

	inf_stream.avail_in = static_cast<unsigned int>(input_buffer_size);
	inf_stream.next_in = reinterpret_cast<const unsigned char*>(input_buffer.data());
	
	if(!set_out)
	{
		inf_stream.next_out=reinterpret_cast<unsigned char*>(buffer);
		inf_stream.avail_out=static_cast<unsigned int>(bsize);
	}	

	Server->Log("mz_inflate(2) avail_in=" + convert(inf_stream.avail_in) + " avail_out=" + convert(inf_stream.avail_out), LL_DEBUG);
	int rc = mz_inflate(&inf_stream, MZ_PARTIAL_FLUSH);

	size_t used = bsize - inf_stream.avail_out;
	Server->Log("rc=" + convert(rc) + " used=" + convert(used)+" avail_in = " + convert(inf_stream.avail_in) + " avail_out = " + convert(inf_stream.avail_out), LL_DEBUG);
	uncompressed_received_bytes+=used;

	if(rc!=MZ_OK && rc!=MZ_STREAM_END)
	{
		Server->Log("Error decompressing stream(2): "+convert(rc));
		has_error=true;
		return 0;
	}

	if(inf_stream.avail_in==0 && inf_stream.avail_out!=0)
	{
		input_buffer_size=0;
	}
	else if(inf_stream.avail_in==0)
	{
		inf_stream.next_in=NULL;
	}

	return used;
}


void CompressedPipe2::ProcessToString(std::string* ret, bool fromLast )
{
	size_t data_pos = 0;
	do 
	{
		if(data_pos+output_incr_size>ret->size())
		{
			ret->resize(ret->size()+output_incr_size);
		}

		size_t avail = ret->size()-data_pos;
		ProcessToBuffer(&(*ret)[data_pos], avail, fromLast);

		if(inf_stream.avail_out!=0)
		{
			ret->resize(ret->size()-inf_stream.avail_out);
		}
		else if(ret->size()>output_max_size)
		{
			return;
		}
		else
		{
			data_pos+=avail;
		}

	} while (inf_stream.avail_out==0);
}

bool CompressedPipe2::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	IScopedLock lock(write_mutex.get());

	assert(buffer != NULL || bsize == 0);
	const char *ptr=buffer;
	size_t cbsize=bsize;
	int64 starttime = Server->getTimeMS();
	do
	{
		cbsize=(std::min)(max_send_size, bsize);

		bsize-=cbsize;
		uncompressed_sent_bytes+=cbsize;

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
			def_stream.next_out = reinterpret_cast<unsigned char*>(comp_buffer.data());

			Server->Log("mz_deflate avail_in=" + convert(def_stream.avail_in) + " avail_out=" + convert(def_stream.avail_out)+" flush="+convert(curr_flush), LL_DEBUG);
			int rc = mz_deflate(&def_stream, curr_flush ? MZ_PARTIAL_FLUSH : MZ_NO_FLUSH);

			if(rc!=MZ_OK && rc!=MZ_STREAM_END)
			{
				Server->Log("Error compressing stream: "+convert(rc));
				has_error=true;
				return false;
			}

			assert(comp_buffer.size() >= def_stream.avail_out);

			size_t used = comp_buffer.size() - def_stream.avail_out;

			Server->Log("rc="+convert(rc)+" used="+convert(used)+" avail_in=" + convert(def_stream.avail_in) + " avail_out=" + convert(def_stream.avail_out), LL_DEBUG);

			int curr_timeout = timeoutms;

			if(curr_timeout>0)
			{
				int64 time_elapsed = Server->getTimeMS()-starttime;
				if(time_elapsed>curr_timeout)
				{
					Server->Log("Timeout after compression", LL_DEBUG);
					return false;
				}
				else
				{
					curr_timeout-=static_cast<int>(time_elapsed);
				}
			}

			if(used>0)
			{
				bool b=cs->Write(comp_buffer.data(), used, curr_timeout, curr_flush);
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
	IScopedLock lock(read_mutex.get());

	if(input_buffer_size>0)
	{
		ProcessToString(ret, true);
		if(!ret->empty())
		{
			return ret->size();
		}
		else if(input_buffer_size==input_buffer.size())
		{
			input_buffer.resize(input_buffer.size()+output_incr_size);
		}
	}

	if(timeoutms==0)
	{
		size_t rc=cs->Read(input_buffer.data()+input_buffer_size, input_buffer.size()-input_buffer_size, timeoutms);
		if(rc==0)
			return 0;

		if(has_error)
		{
			return 0;
		}
		input_buffer_size+=rc;
		ProcessToString(ret, false);
		return ret->size();
	}
	else if(timeoutms==-1)
	{
		size_t rc;
		do
		{
			rc=cs->Read(input_buffer.data()+input_buffer_size, input_buffer.size()-input_buffer_size, timeoutms);
			if(rc==0)
				return 0;

			if(has_error)
			{
				return 0;
			}

			input_buffer_size+=rc;
			ProcessToString(ret, false);
			rc=ret->size();
		}
		while(rc==0);
		return rc;
	}

	int64 starttime=Server->getTimeMS();
	size_t rc;
	do
	{
		int left=timeoutms-static_cast<int>(Server->getTimeMS()-starttime);

		rc=cs->Read(input_buffer.data()+input_buffer_size, input_buffer.size()-input_buffer_size, left);
		if(rc==0)
			return 0;

		if(has_error)
		{
			return 0;
		}
		input_buffer_size+=rc;
		ProcessToString(ret, false);
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
	if(input_buffer_size>0)
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
	int64 encryption_overhead=0;
	InternetServicePipe2* isp2 = dynamic_cast<InternetServicePipe2*>(getRealPipe());
	if(isp2!=NULL)
	{
		encryption_overhead=isp2->getEncryptionOverheadBytes();
		Server->Log("Encryption overhead: "+PrettyPrintBytes(encryption_overhead));
	}

	return getUncompressedSentBytes()+getUncompressedReceivedBytes()-encryption_overhead;
}
