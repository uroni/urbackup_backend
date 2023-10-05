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

#ifndef NO_ZSTD_COMPRESSION

#include "CompressedPipeZstd.h"
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
#include "os_functions.h"

#define VLOG(x)


const size_t max_send_size=20000;
const size_t output_incr_size=8192;
const size_t output_incr_size_init = 32*1024 - 1000;
const size_t output_max_size=32*1024;
const size_t init_comp_buffer_size = 1000;
const size_t init_comp_buffer_size_highmem = 512*1024;

CompressedPipeZstd::CompressedPipeZstd(IPipe *cs, int compression_level)
	: cs(cs), has_error(false),
	uncompressed_sent_bytes(0), uncompressed_received_bytes(0), sent_flushes(0),
	input_buffer_size(0), read_mutex(Server->createMutex()), write_mutex(Server->createMutex()),
	last_send_time(Server->getTimeMS()),
	inf_stream(ZSTD_createDStream()),
	def_stream(ZSTD_createCCtx()),
	usage_mutex(Server->createMutex()),
	compression_level(compression_level)
{
	comp_buffer_size = init_comp_buffer_size;
	comp_buffer = new char[comp_buffer_size];
	input_buffer.resize(init_comp_buffer_size);
	destroy_cs=false;

	if(inf_stream==NULL)
	{
		throw std::runtime_error("Error initializing compression stream");
	}
	if(def_stream==NULL)
	{
		throw std::runtime_error("Error initializing decompression stream");
	}

	size_t err = ZSTD_CCtx_setParameter(def_stream, ZSTD_c_compressionLevel, -3);

	if (ZSTD_isError(err))
	{
		throw std::runtime_error(std::string("Error setting zstd compression level. ") + ZSTD_getErrorName(err));
	}

	usage_add = "level=-3 veryLowMem";
}

CompressedPipeZstd::~CompressedPipeZstd(void)
{
	delete[] comp_buffer;
	ZSTD_freeDStream(inf_stream);
	ZSTD_freeCCtx(def_stream);
	
	if(destroy_cs)
	{
		Server->destroy(cs);
	}
}

size_t CompressedPipeZstd::Read(char *buffer, size_t bsize, int timeoutms)
{
	IScopedLock lock(read_mutex.get());
	VLOG(Server->Log("Read bsize=" + convert(bsize) + " timeoutms=" + convert(timeoutms)+" input_buffer_size="+convert(input_buffer_size), LL_DEBUG));

	if(input_buffer_size>0)
	{
		size_t rc = ProcessToBuffer(buffer, bsize, true);
		if(rc>0)
		{
			return rc;
		}
		else if(input_buffer_size==input_buffer.size())
		{
			if(input_buffer_size == init_comp_buffer_size)
				input_buffer.resize(input_buffer.size() + output_incr_size_init);
			else
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
	size_t rc=0;
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

size_t CompressedPipeZstd::ProcessToBuffer(char *buffer, size_t bsize, bool fromLast)
{
	VLOG(Server->Log("bsize=" + convert(bsize) + " fromLast=" + convert(fromLast), LL_DEBUG));
	bool set_out=false;
	if(fromLast)
	{
		set_out=true;

		ZSTD_outBuffer outBuffer;
		outBuffer.dst = buffer;
		outBuffer.size = bsize;
		outBuffer.pos = 0;

		VLOG(Server->Log("ZSTD_decompressStream(1) avail_in=" + convert(inf_in_last.size - inf_in_last.pos) + " avail_out=" + convert(bsize), LL_DEBUG));

		size_t rc = ZSTD_decompressStream(inf_stream, &outBuffer, &inf_in_last);
		
		assert(bsize >= outBuffer.size - outBuffer.pos);
		size_t used = outBuffer.pos;
		uncompressed_received_bytes+=used;

		VLOG(Server->Log("rc=" + convert(rc) + " used=" + convert(used) + " avail_in = " + convert(inf_in_last.size - inf_in_last.pos) + " avail_out = " + convert(outBuffer.size - outBuffer.pos), LL_DEBUG));

		if(ZSTD_isError(rc))
		{
			Server->Log("Error decompressing stream(1): " + convert(rc)
				+ " Err: "+ZSTD_getErrorName(rc), LL_ERROR);
			has_error=true;
			return 0;
		}

		if(inf_in_last.size==inf_in_last.pos && outBuffer.pos!= outBuffer.size)
		{
			input_buffer_size=0;
		}

		return used;
	}

	inf_in_last.src = input_buffer.data();
	inf_in_last.pos = 0;
	inf_in_last.size = input_buffer_size;
	
	ZSTD_outBuffer outBuffer;
	outBuffer.dst = buffer;
	outBuffer.size = bsize;
	outBuffer.pos = 0;

	VLOG(Server->Log("ZSTD_decompressStream(2) avail_in=" + convert(input_buffer_size) + " avail_out=" + convert(bsize), LL_DEBUG));
	size_t rc = ZSTD_decompressStream(inf_stream, &outBuffer, &inf_in_last);

	size_t used = outBuffer.pos;
	VLOG(Server->Log("rc=" + convert(rc) + " used=" + convert(used)+" avail_in = " + convert(inf_in_last.size - inf_in_last.pos) + " avail_out = " + convert(outBuffer.size - outBuffer.pos), LL_DEBUG));
	uncompressed_received_bytes+=used;

	if (ZSTD_isError(rc))
	{
		Server->Log("Error decompressing stream(2): "+convert(rc)
			+ " Err: " + ZSTD_getErrorName(rc), LL_ERROR);
		has_error=true;
		return 0;
	}

	if(inf_in_last.size == inf_in_last.pos && outBuffer.pos != outBuffer.size)
	{
		input_buffer_size=0;
	}

	return used;
}


void CompressedPipeZstd::ProcessToString(std::string* ret, bool fromLast )
{
	size_t data_pos = 0;
	do 
	{
		if(data_pos+output_incr_size>ret->size())
		{
			ret->resize(ret->size()+output_incr_size);
		}

		size_t avail = ret->size()-data_pos;
		size_t used = ProcessToBuffer(&(*ret)[data_pos], avail, fromLast);

		if(used<avail)
		{
			ret->resize(ret->size()-(avail-used));
		}
		else if(ret->size()>output_max_size)
		{
			return;
		}
		else
		{
			data_pos+=avail;
		}

		fromLast = true;

	} while (input_buffer_size!=0 && !has_error);
}

bool CompressedPipeZstd::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	return WriteInt(buffer, bsize, timeoutms, flush ? ZSTD_e_flush : ZSTD_e_continue);
}

bool CompressedPipeZstd::WriteInt(const char *buffer, size_t bsize, int timeoutms, ZSTD_EndDirective flush)
{
	IScopedLock lock(write_mutex.get());

	assert(buffer != NULL || bsize == 0);
	const char* ptr=buffer;
	size_t cbsize=bsize;
	int64 starttime = Server->getTimeMS();
	do
	{
		cbsize=(std::min)(max_send_size, bsize);

		bsize-=cbsize;
		uncompressed_sent_bytes+=cbsize;

		bool has_next = bsize>0;
		ZSTD_EndDirective curr_flush = has_next ? ZSTD_e_continue : flush;

		int64 flush_timeout = adaptive.get()==NULL ? 1000 : adaptive->flush_timeout;

		if (curr_flush== ZSTD_e_continue
			&& Server->getTimeMS() - last_send_time > flush_timeout)
		{
			curr_flush = ZSTD_e_flush;
		}

		if(curr_flush)
		{
			++sent_flushes;
		}

		
		ZSTD_inBuffer inbuf;
		inbuf.src = ptr;
		inbuf.pos = 0;
		inbuf.size = cbsize;

		ZSTD_outBuffer outbuf;
		size_t rc;

		do 
		{		
			outbuf.dst = comp_buffer;
			outbuf.pos = 0;
			outbuf.size = comp_buffer_size;

			size_t prev_inbuf_pos = inbuf.pos;

			size_t to_flush_now = ZSTD_toFlushNow(def_stream);
			VLOG(Server->Log("ZSTD_compressStream2 avail_in=" + convert(inbuf.size-inbuf.pos) + " avail_out=" + convert(outbuf.size)+" flush="+convert((int)curr_flush), LL_DEBUG));
			rc = ZSTD_compressStream2(def_stream, &outbuf, &inbuf, curr_flush);

			if(ZSTD_isError(rc))
			{
				Server->Log("Error compressing stream: "+convert(rc)
					+ " Err: " + ZSTD_getErrorName(rc), LL_ERROR);
				has_error=true;
				return false;
			}

			if (adaptive.get()!=NULL)
				++adaptive->zstd_input_presented;

			if (adaptive.get() != NULL
				&& prev_inbuf_pos == inbuf.pos)
				++adaptive->zstd_input_blocked;

			assert(comp_buffer_size >= outbuf.size - outbuf.pos);

			size_t used = outbuf.pos;

			VLOG(Server->Log("rc="+convert(rc)+" used="+convert(used)+" avail_in=" + convert(inbuf.size - inbuf.pos) + " avail_out=" + convert(outbuf.size - outbuf.pos), LL_DEBUG));

			int curr_timeout = timeoutms;

			if(curr_timeout>0)
			{
				int64 time_elapsed = Server->getTimeMS()-starttime;
				if(time_elapsed>curr_timeout)
				{
					VLOG(Server->Log("Timeout after compression", LL_DEBUG));
					return false;
				}
				else
				{
					curr_timeout-=static_cast<int>(time_elapsed);
				}
			}

			bool ret;
			if(used>0)
			{
				last_send_time = Server->getTimeMS();

				bool b=cs->Write(comp_buffer, used, curr_timeout, curr_flush!= ZSTD_e_continue);
				if(!b)
					return false;
			}
			else if(!has_next && flush)
			{
				ret = cs->Flush(curr_timeout);
			}

			if (adaptive.get() != NULL)
			{
				ZSTD_frameProgression zfp = ZSTD_getFrameProgression(def_stream);
				int comp_mod = 0;

				if (zfp.currentJobID > 1
					&& zfp.currentJobID>adaptive->last_job_id)
				{
					uint64 new_flushed = zfp.flushed - adaptive->zfp_prev.flushed;
					uint64 new_produced = zfp.produced - adaptive->zfp_prev.produced;

					if (zfp.consumed == adaptive->zfp_prev.consumed
						&& zfp.currentJobID == 0)
					{
						Server->Log("Buffers full. Increasing compression ratio", LL_DEBUG);
						comp_mod = 1;
					}

					adaptive->zfp_prev = zfp;

					if (new_produced > (new_flushed * 9 / 8)
						&& to_flush_now > 0)
					{
						Server->Log("Compression faster than flush ("+ PrettyPrintBytes(new_produced)+" > "+ PrettyPrintBytes(new_flushed)+"). Increasing compression level", LL_DEBUG);
						comp_mod = 1;
					}

					if (zfp.currentJobID > adaptive->n_zstd_workers + 1)
					{
						if (adaptive->zstd_input_blocked == 0)
						{
							Server->Log("Input not blocked. Increasing compression level", LL_DEBUG);
							comp_mod = 1;
						}
						else
						{
							uint64 new_ingested = zfp.ingested - adaptive->zfp_prev_corr.ingested;
							uint64 new_consumed = zfp.consumed - adaptive->zfp_prev_corr.consumed;
							uint64 new_produced = zfp.produced - adaptive->zfp_prev_corr.produced;
							uint64 new_flushed = zfp.flushed - adaptive->zfp_prev_corr.flushed;

							if (adaptive->zstd_input_blocked > adaptive->zstd_input_presented / 8
								&& new_flushed * 33 / 32 > new_produced
								&& new_ingested * 33 / 32 > new_consumed)
							{
								Server->Log("in " + PrettyPrintBytes(new_ingested) + " >= " + PrettyPrintBytes(new_consumed) + " comp " + PrettyPrintBytes(new_produced) + " <= " + PrettyPrintBytes(new_flushed) + ". Decreasing compression level", LL_DEBUG);
								comp_mod = -1;
							}

							adaptive->zfp_prev_corr = zfp;
						}

						adaptive->zstd_input_blocked = 0;
						adaptive->zstd_input_presented = 0;
					}

					if (comp_mod == -1
						&& adaptive->comp_level > 1)
					{
						--adaptive->comp_level;
						Server->Log("Lowering compression level to " + convert(adaptive->comp_level), LL_DEBUG);
						ZSTD_CCtx_setParameter(def_stream, ZSTD_c_compressionLevel, adaptive->comp_level);
					}
					else if (comp_mod == 1
						&& adaptive->comp_level < ZSTD_maxCLevel())
					{
						++adaptive->comp_level;
						Server->Log("Increasing compression level to "+convert(adaptive->comp_level), LL_DEBUG);
						ZSTD_CCtx_setParameter(def_stream, ZSTD_c_compressionLevel, adaptive->comp_level);
					}
				}

				adaptive->last_job_id = zfp.currentJobID;
			}

			if (used==0
				&& !has_next
				&& flush)
				return ret;

		} while(inbuf.pos<inbuf.size || outbuf.pos==outbuf.size || (curr_flush!= ZSTD_e_continue && rc!=0) );

		ptr+=cbsize;
		
	} while(bsize>0);

	return true;
}

size_t CompressedPipeZstd::Read(std::string *ret, int timeoutms)
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
			if (input_buffer_size == init_comp_buffer_size)
				input_buffer.resize(input_buffer.size() + output_incr_size_init);
			else
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

bool CompressedPipeZstd::Write(const std::string &str, int timeoutms, bool flush)
{
	return Write(str.c_str(), str.size(), timeoutms, flush);
}

/**
* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
*/
bool CompressedPipeZstd::isWritable(int timeoutms)
{
	return cs->isWritable(timeoutms);
}

bool CompressedPipeZstd::isReadable(int timeoutms)
{
	if(input_buffer_size>0)
		return true;
	else
		return cs->isReadable(timeoutms);
}

bool CompressedPipeZstd::hasError(void)
{
	return cs->hasError() || has_error;
}

void CompressedPipeZstd::shutdown(void)
{
	cs->shutdown();
}

size_t CompressedPipeZstd::getNumWaiters() {
	return cs->getNumWaiters();
}

size_t CompressedPipeZstd::getNumElements(void)
{
	return cs->getNumElements();
}

void CompressedPipeZstd::destroyBackendPipeOnDelete(bool b)
{
	destroy_cs=b;
}

IPipe *CompressedPipeZstd::getRealPipe(void)
{
	return cs;
}

void CompressedPipeZstd::addThrottler(IPipeThrottler *throttler)
{
	cs->addThrottler(throttler);
}

void CompressedPipeZstd::addOutgoingThrottler(IPipeThrottler *throttler)
{
	cs->addOutgoingThrottler(throttler);
}

void CompressedPipeZstd::addIncomingThrottler(IPipeThrottler *throttler)
{
	cs->addIncomingThrottler(throttler);
}

_i64 CompressedPipeZstd::getTransferedBytes(void)
{
	return cs->getTransferedBytes();
}

void CompressedPipeZstd::resetTransferedBytes(void)
{
	cs->resetTransferedBytes();
}

bool CompressedPipeZstd::Flush( int timeoutms/*=-1 */ )
{
	return Write(NULL, 0, timeoutms, true);
}

int64 CompressedPipeZstd::getUncompressedReceivedBytes()
{
	return uncompressed_received_bytes;
}

int64 CompressedPipeZstd::getUncompressedSentBytes()
{
	return uncompressed_sent_bytes;
}

int64 CompressedPipeZstd::getSentFlushes()
{
	return sent_flushes;
}

_i64 CompressedPipeZstd::getRealTransferredBytes()
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

void CompressedPipeZstd::setUsageString(const std::string& str)
{
	IScopedLock lock(usage_mutex.get());
	usage_curr = str;
	cs->setUsageString(str+ (usage_add.empty()?std::string(): (" "+usage_add) ) );
}

bool CompressedPipeZstd::setCompressionSettings(const SCompressionSettings& params)
{
	IScopedLock lock_wr(write_mutex.get());
	IScopedLock lock_usage(usage_mutex.get());
	usage_add.clear();

	if (!WriteInt(NULL, 0, params.send_timeout, ZSTD_e_end))
	{
		return false;
	}
	int compression_level;
	size_t err = ZSTD_CCtx_getParameter(def_stream, ZSTD_c_compressionLevel, &compression_level);
	if (ZSTD_isError(err))
	{
		Server->Log(std::string("Error getting compression level to set compression settings. ") + ZSTD_getErrorName(err), LL_ERROR);
		return false;
	}

	err = ZSTD_CCtx_reset(def_stream, ZSTD_reset_parameters);

	if (ZSTD_isError(err))
	{
		Server->Log(std::string("Error resetting stream to set compression settings. ") + ZSTD_getErrorName(err), LL_ERROR);
		return false;
	}

	err = ZSTD_CCtx_setParameter(def_stream, ZSTD_c_compressionLevel, compression_level);

	usage_add += "level=" + convert(compression_level);

	if (ZSTD_isError(err))
	{
		Server->Log(std::string("Error setting zstd compression level after resetting to set compression settings. ") + ZSTD_getErrorName(err), LL_ERROR);
		return false;
	}

	if (params.mem == Compression_LowMem)
	{
		usage_add += " lowMem";
	}
	else
	{
		// TBD
		usage_add += " highMem";
	}

	if (params.buffer_size > 0
		&& params.buffer_size != std::string::npos)
	{
		comp_buffer_size = params.buffer_size;
	}
	else
	{
		comp_buffer_size = params.mem == Compression_LowMem ? 
			init_comp_buffer_size : init_comp_buffer_size_highmem;
	}

	delete[] comp_buffer;
	comp_buffer = new char[comp_buffer_size];
	usage_add += " compBuffer=" + PrettyPrintBytes(comp_buffer_size);

	size_t n_threads = params.n_threads;
	if (n_threads == std::string::npos)
	{
		size_t zstd_max_threads = 4;
		n_threads = (std::min)(zstd_max_threads , os_get_num_cpus());
	}

	if (n_threads > 1 )
	{
		usage_add += " threads=" + convert(n_threads);
		size_t err = ZSTD_CCtx_setParameter(def_stream, ZSTD_c_nbWorkers, static_cast<int>(n_threads));

		if (ZSTD_isError(err))
		{
			Server->Log(std::string("Error setting zstd workers. ") + ZSTD_getErrorName(err), LL_ERROR);
			setUsageString(usage_curr);
			return false;
		}
	}

	if (params.adaptive_compression)
	{
		adaptive.reset(new ZstdAdaptive);

		usage_add += "adaptive flush_timeout="+convert(params.adaptive_comp_flush_timeout)+" n_threads="+convert(n_threads);
		adaptive->flush_timeout = params.adaptive_comp_flush_timeout;
		memset(&adaptive->zfp_prev, 0, sizeof(ZSTD_frameProgression));
		memset(&adaptive->zfp_prev_corr, 0, sizeof(ZSTD_frameProgression));
		adaptive->n_zstd_workers = n_threads;
		adaptive->zstd_input_blocked = 0;
		adaptive->zstd_input_presented = 0;
		adaptive->comp_level = compression_level;
	}
	else
	{
		adaptive.reset();
	}

	setUsageString(usage_curr);
	return true;
}

#endif //NO_ZSTD_COMPRESSION

