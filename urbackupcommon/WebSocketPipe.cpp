/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2020 Martin Raiber
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

#include "WebSocketPipe.h"
#include "../Interface/Server.h"
#include <algorithm>
#include <assert.h>
#include "../stringtools.h"
#include <memory.h>

WebSocketPipe::WebSocketPipe(IPipe* pipe, const bool mask_writes, const bool expect_read_mask, std::string pipe_add, bool destroy_pipe)
	: pipe(pipe), mask_writes(mask_writes), expect_read_mask(expect_read_mask), has_error(false),
	pipe_add(pipe_add), read_state(EReadState_Header1), destroy_pipe(destroy_pipe),
	read_mutex(Server->createMutex()), write_mutex(Server->createMutex())
{
	memset(masking_key, 0, sizeof(masking_key));

	if (mask_writes)
	{
		char zero_mask[4] = {};
		/*
		* Just use a fixed non-random masking key. We are not a browser, so the security implications
		* are a bit different
		*/
		while (memcmp(zero_mask, masking_key, sizeof(masking_key)) == 0)
		{
			Server->randomFill(masking_key, sizeof(masking_key));
		}
	}
}

WebSocketPipe::~WebSocketPipe()
{
	if (destroy_pipe)
		delete pipe;
}

size_t WebSocketPipe::Read(char* buffer, size_t bsize, int timeoutms)
{
	IScopedLock lock(read_mutex.get());

	if (!pipe_add.empty())
	{
		size_t consumed_out = 0;
		size_t data_size = consume(&pipe_add[0], (std::min)(bsize, pipe_add.size()), timeoutms, &consumed_out);

		if (data_size > 0)
		{
			memcpy(buffer, pipe_add.data(), data_size);
		}

		if (consumed_out > 0)
		{
			pipe_add.erase(0, consumed_out);
		}

		if (data_size > 0)
		{
			return data_size;
		}
	}

	int64 starttime = 0;

	if (timeoutms > 0)
	{
		starttime = Server->getTimeMS();
	}

	do
	{
		int curr_timeoutms = static_cast<int>(timeoutms > 0 ? (timeoutms - (Server->getTimeMS() - starttime)) : timeoutms);
		size_t read = pipe->Read(buffer, bsize, curr_timeoutms);

		if (read > 0)
		{
			size_t data_size = consume(buffer, read, curr_timeoutms, NULL);

			if (data_size > 0)
			{
				return data_size;
			}
		}
		else
		{
			return 0;
		}

	} while (timeoutms == -1 || (timeoutms > 0 && Server->getTimeMS() - starttime < timeoutms));

	return 0;
}

bool WebSocketPipe::Write(const char* buffer, size_t bsize, int timeoutms, bool flush)
{
	IScopedLock lock(write_mutex.get());

	size_t payload_len_size = 1;

	if (bsize > 65535)
	{
		payload_len_size = 8;
	}
	else if (bsize > 125)
	{
		payload_len_size = 2;
	}

	char header[2 + 8 + 4];

	unsigned char bits = 0;

	bits |= 1 << 7; //FIN bit

	unsigned char opcode = 2; //binary frame;

	bits |= opcode;

	header[0] = static_cast<char>(bits);

	size_t header_pos = 1;

	if (bsize > 65535)
	{
		header[header_pos] = 127;
		++header_pos;

		uint64 payload_size = bsize;
		memcpy(&header[header_pos], &payload_size, sizeof(payload_size));
		header_pos += sizeof(payload_size);
	}
	else if (bsize > 125)
	{
		header[header_pos] = 126;
		++header_pos;

		unsigned short payload_size = static_cast<unsigned short>(bsize);
		memcpy(&header[header_pos], &payload_size, sizeof(payload_size));
		header_pos += sizeof(payload_size);
	}
	else
	{
		header[header_pos] = static_cast<char>(bsize);
		++header_pos;
	}

	if (mask_writes)
	{
		header[1] |= 1 << 7;

		memcpy(&header[header_pos], &masking_key, sizeof(masking_key));
		header_pos += sizeof(masking_key);

		std::vector<char> new_buf;
		new_buf.resize(header_pos + bsize);

		memcpy(new_buf.data(), header, header_pos);

		Server->Log("Masking key: " + convert(*((unsigned int*)masking_key)));
		for (size_t i = 0; i < bsize; ++i)
		{
			size_t j = i % 4;
			new_buf[header_pos + i] = buffer[i] ^ masking_key[j];
		}

		return pipe->Write(new_buf.data(), header_pos + bsize, timeoutms, flush);
	}
	else
	{
		if (!pipe->Write(header, header_pos, timeoutms, false))
		{
			return false;
		}

		return pipe->Write(buffer, bsize, timeoutms, flush);
	}
}

size_t WebSocketPipe::Read(std::string* ret, int timeoutms)
{
	IScopedLock lock(read_mutex.get());

	if (!pipe_add.empty())
	{
		size_t consumed_out = 0;
		size_t data_size = consume(&pipe_add[0], pipe_add.size(), timeoutms, &consumed_out);

		if (data_size > 0)
		{
			ret->assign(pipe_add.data(), data_size);
		}

		if (consumed_out > 0)
		{
			pipe_add.erase(0, consumed_out);
		}

		if (data_size > 0)
		{
			return data_size;
		}
	}

	int64 starttime = 0;

	if (timeoutms > 0)
	{
		starttime = Server->getTimeMS();
	}

	do
	{
		int curr_timeoutms = static_cast<int>(timeoutms > 0 ? (timeoutms - (Server->getTimeMS() - starttime)) : timeoutms);
		size_t read = pipe->Read(ret, curr_timeoutms);

		if (read > 0)
		{
			size_t data_size = consume(&(*ret)[0], ret->size(), curr_timeoutms, NULL);

			if (data_size > 0)
			{
				ret->resize(data_size);
				return ret->size();
			}
		}
		else
		{
			return 0;
		}

	} while (timeoutms < 0
		|| (timeoutms > 0 && Server->getTimeMS() - starttime < timeoutms));

	return 0;
}

bool WebSocketPipe::isReadable(int timeoutms)
{
	{
		IScopedLock lock(read_mutex.get());

		if (!pipe_add.empty())
			return true;
	}

	return pipe->isReadable(timeoutms);
}

size_t WebSocketPipe::consume(char* buffer, size_t bsize, int write_timeoutms, size_t* consumed_out)
{
	size_t consumed = 0;
	size_t out_off = 0;
	while (bsize > consumed)
	{
		switch (read_state)
		{
		case EReadState_Header1:
		{
			header_bits1 = buffer[consumed];
			++consumed;
			read_state = EReadState_HeaderSize1;

			unsigned char opcode = get_opcode();

			if (opcode != 0
				&& opcode != 1
				&& opcode != 2
				&& opcode != 8
				&& opcode != 9
				&& opcode != 10)
			{
				has_error = true;
			}

			if (!(header_bits1 & 1)
				&& opcode != 0
				&& opcode != 1
				&& opcode != 2)
			{
				has_error = true;
			}

		}break;
		case EReadState_HeaderSize1:
		{
			header_bits2 = buffer[consumed];

			if (expect_read_mask
				&& !has_read_mask())
			{
				has_error = true;
			}

			unsigned char tmp_payload_size = header_bits2 & 0x7F;
			++consumed;

			if (tmp_payload_size < 126)
			{
				payload_size = tmp_payload_size;

				if (has_read_mask())
				{
					read_state = EReadState_HeaderMask;
					remaining_size_bytes = 4;
					consumed_size_bytes = 0;
					curr_has_read_mask = true;
				}
				else
				{
					curr_has_read_mask = false;
					read_state = EReadState_Body;
				}
			}
			else if (tmp_payload_size == 126)
			{
				remaining_size_bytes = 2;
				consumed_size_bytes = 0;
				payload_size = 0;
				read_state = EReadState_HeaderSize2;
			}
			else if (tmp_payload_size == 127)
			{
				remaining_size_bytes = 8;
				consumed_size_bytes = 0;
				payload_size = 0;
				read_state = EReadState_HeaderSize2;
			}
			else
			{
				has_error = true;
			}

		}break;
		case EReadState_HeaderSize2:
		{
			//PERF: In EReadState_HeaderSize2 and EReadState_HeaderMask read multiple via memcpy if available
			unsigned char size_byte = buffer[consumed];
			++consumed;
			--remaining_size_bytes;

			payload_size |= size_byte << (consumed_size_bytes * 8);
			++consumed_size_bytes;

			if (remaining_size_bytes == 0)
			{
				if (has_read_mask())
				{
					read_state = EReadState_HeaderMask;
					curr_has_read_mask = true;
					remaining_size_bytes = 4;
					consumed_size_bytes = 0;
				}
				else
				{
					curr_has_read_mask = false;
					read_state = EReadState_Body;
				}
			}
		}break;
		case EReadState_HeaderMask:
		{
			unsigned char mask_byte = buffer[consumed];
			++consumed;
			--remaining_size_bytes;

			read_mask[consumed_size_bytes] = mask_byte;
			++consumed_size_bytes;

			if (remaining_size_bytes == 0)
			{
				read_state = EReadState_Body;
				read_mask_idx = 0;
			}
		} break;
		case EReadState_Body:
		{
			size_t toread = static_cast<size_t>((std::min)(static_cast<uint64>(bsize - consumed), payload_size));
			payload_size -= toread;

			unsigned char opcode = get_opcode();

			if (opcode != 0 && opcode!=1 && opcode != 2)
			{
				//Ignore payload
				consumed += toread;
			}
			else if (out_off == consumed)
			{
				if (curr_has_read_mask)
				{
					for (size_t i = 0; i < toread; ++i)
					{
						buffer[out_off] = buffer[out_off] ^ read_mask[read_mask_idx % 4];
						++read_mask_idx;
						++out_off;
					}
					consumed = out_off;
				}
				else
				{
					consumed += toread;
					out_off += toread;
				}
			}
			else
			{
				assert(out_off < consumed);
				if (curr_has_read_mask)
				{
					for (size_t i = 0; i < toread; ++i)
					{
						buffer[out_off] = buffer[consumed] ^ read_mask[read_mask_idx % 4];
						++read_mask_idx;
						++out_off;
						++consumed;
					}
				}
				else
				{
					memmove(&buffer[out_off], &buffer[consumed], toread);
					consumed += toread;
					out_off += toread;
				}
			}

			if (payload_size == 0)
			{
				read_state = EReadState_Header1;

				unsigned char opcode = get_opcode();

				if (opcode == 8)
				{
					//Close
					char msg[2];
					msg[0] = header_bits1;
					msg[1] = header_bits2 | (1<<7);
					pipe->Write(msg, 2, write_timeoutms, true);
					has_error = true;
				}
				else if (opcode == 9)
				{
					//Close
					char msg[2];
					unsigned char opcode = 10; //pong
					msg[0] = opcode | (1 << 7);
					msg[1] = static_cast<char>(1<<7);
					if (!pipe->Write(msg, 2, write_timeoutms, true))
					{
						has_error = true;
					}
				}
			}
		}break;
		}
	}

	if (consumed_out != NULL)
		*consumed_out = consumed;

	return out_off;
}
