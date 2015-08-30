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

#include "CompressedPipe.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "../Interface/Server.h"
#include <limits.h>
#include <memory.h>
#include <string.h>
#include "../stringtools.h"

extern ICryptoFactory *crypto_fak;
const size_t max_send_size=20000;

CompressedPipe::CompressedPipe(IPipe *cs, int compression_level)
	: cs(cs), has_error(false)
{
	comp=crypto_fak->createZlibCompression(compression_level);
	decomp=crypto_fak->createZlibDecompression();
	recv_state=RS_LENGTH;
	decomp_buffer_pos=0;
	decomp_read_pos=0;
	comp_buffer.resize(100);
	message_len_byte=0;
	destroy_cs=false;
}

CompressedPipe::~CompressedPipe(void)
{
	decomp->Remove();
	comp->Remove();
	if(destroy_cs)
	{
		Server->destroy(cs);
	}
}

size_t CompressedPipe::ReadToBuffer(char *buffer, size_t bsize)
{
	if(decomp_read_pos<decomp_buffer_pos)
	{
		size_t toread=(std::min)(bsize, decomp_buffer_pos-decomp_read_pos);
		memcpy(buffer, &decomp_buffer[decomp_read_pos], toread);
		decomp_read_pos+=toread;

		if(decomp_read_pos==decomp_buffer_pos)
		{
			decomp_read_pos=0;
			decomp_buffer_pos=0;
		}

		return toread;
	}
	return 0;
}

size_t CompressedPipe::ReadToString(std::string *ret)
{
	if(decomp_read_pos<decomp_buffer_pos)
	{
		size_t toread=decomp_buffer_pos-decomp_read_pos;
		ret->resize(toread);
		memcpy((char*)ret->c_str(), &decomp_buffer[decomp_read_pos], toread);
		decomp_read_pos=0;
		decomp_buffer_pos=0;
		return toread;
	}
	return 0;
}

size_t CompressedPipe::Read(char *buffer, size_t bsize, int timeoutms)
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

void CompressedPipe::Process(const char *buffer, size_t bsize)
{
	const char *cptr=buffer;
	size_t b_left=bsize;
	while(b_left>0)
	{
		if(recv_state==RS_LENGTH)
		{
			size_t used_bytes=(std::min)(b_left, sizeof(_u16)-message_len_byte);
			memcpy(((char*)&message_len)+message_len_byte, cptr, used_bytes);
			message_len_byte+=used_bytes;

			if(message_len_byte==sizeof(_u16))
			{
				message_len = little_endian(message_len);
				if(message_len>0)
				{
					recv_state=RS_CONTENT;
					if(input_buffer.size()<message_len)
						input_buffer.resize(message_len);

					input_buffer_pos=0;
					message_left=message_len;
				}
				message_len_byte=0;
			}

			b_left-=used_bytes;
			cptr+=used_bytes;
		}
		else if(recv_state==RS_CONTENT)
		{
			if(b_left>=message_left && input_buffer_pos==0)
			{
				decomp_buffer_pos+=decomp->decompress(cptr, message_left, &decomp_buffer, true, decomp_buffer_pos, &has_error);
				recv_state=RS_LENGTH;
				message_len_byte=0;
				b_left-=message_left;
				cptr+=message_left;
			}
			else
			{
				size_t used_bytes=(std::min)(b_left, message_left);
				memcpy(&input_buffer[input_buffer_pos], cptr, used_bytes);
				input_buffer_pos+=used_bytes;
				message_left-=used_bytes;
				b_left-=used_bytes;
				cptr+=used_bytes;

				if(message_left==0)
				{
					decomp_buffer_pos+=decomp->decompress(&input_buffer[0], message_len, &decomp_buffer, true, decomp_buffer_pos, &has_error);
					recv_state=RS_LENGTH;
					message_len_byte=0;
				}
			}
		}
	}
}

bool CompressedPipe::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	const char *ptr=buffer;
	size_t cbsize=bsize;
	do
	{
		cbsize=(std::min)(max_send_size, bsize);

		bsize-=cbsize;

		bool has_next = bsize>0;

		_u16 rc=(_u16)comp->compress(ptr, cbsize, &comp_buffer, flush, sizeof(_u16));
		if(rc>0)
		{
			*((_u16*)&comp_buffer[0])=little_endian(rc);
			bool b=cs->Write(&comp_buffer[0], rc+sizeof(_u16), timeoutms);
			if(!b)
				return false;
		}
		else if(!has_next && flush)
		{
			return cs->Flush();
		}

		ptr+=cbsize;
		
	} while(bsize>0);

	return true;
}

size_t CompressedPipe::Read(std::string *ret, int timeoutms)
{
	size_t rc=ReadToString(ret);
	if(rc>0) return rc;

	if(timeoutms==0)
	{
		rc=cs->Read(ret, timeoutms);
		Process(ret->c_str(), ret->size());
		if(has_error)
		{
			return 0;
		}
		return ReadToString(ret);
	}
	else if(timeoutms==-1)
	{
		do
		{
			rc=cs->Read(ret, timeoutms);
			if(rc==0)
				return 0;

			Process(ret->c_str(), ret->size());
			if(has_error)
			{
				return 0;
			}
			rc=ReadToString(ret);
		}
		while(rc==0);
		return rc;
	}

	int64 starttime=Server->getTimeMS();
	do
	{
		int left=timeoutms-static_cast<int>(Server->getTimeMS()-starttime);

		rc=cs->Read(ret, left);
		if(rc==0)
			return 0;

		Process(ret->c_str(), ret->size());
		if(has_error)
		{
			return 0;
		}
		rc=ReadToString(ret);
	}
	while(rc==0 && Server->getTimeMS()-starttime<static_cast<int64>(timeoutms));

	return rc;
}

bool CompressedPipe::Write(const std::string &str, int timeoutms, bool flush)
{
	return Write(str.c_str(), str.size(), timeoutms, flush);
}

/**
* @param timeoutms -1 for blocking >=0 to block only for x ms. Default: nonblocking
*/
bool CompressedPipe::isWritable(int timeoutms)
{
	return cs->isWritable(timeoutms);
}

bool CompressedPipe::isReadable(int timeoutms)
{
	if(decomp_read_pos<decomp_buffer_pos)
		return true;
	else
		return cs->isReadable(timeoutms);
}

bool CompressedPipe::hasError(void)
{
	return cs->hasError() || has_error;
}

void CompressedPipe::shutdown(void)
{
	cs->shutdown();
}

size_t CompressedPipe::getNumElements(void)
{
	return cs->getNumElements();
}

void CompressedPipe::destroyBackendPipeOnDelete(bool b)
{
	destroy_cs=b;
}

IPipe *CompressedPipe::getRealPipe(void)
{
	return cs;
}

void CompressedPipe::addThrottler(IPipeThrottler *throttler)
{
	cs->addThrottler(throttler);
}

void CompressedPipe::addOutgoingThrottler(IPipeThrottler *throttler)
{
	cs->addOutgoingThrottler(throttler);
}

void CompressedPipe::addIncomingThrottler(IPipeThrottler *throttler)
{
	cs->addIncomingThrottler(throttler);
}

_i64 CompressedPipe::getTransferedBytes(void)
{
	return cs->getTransferedBytes();
}

void CompressedPipe::resetTransferedBytes(void)
{
	cs->resetTransferedBytes();
}

bool CompressedPipe::Flush( int timeoutms/*=-1 */ )
{
	return Write(NULL, 0, timeoutms, true);
}
