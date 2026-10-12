/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2026 Martin Raiber
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

#include "ImageReadFile.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <algorithm>
#include <vector>

namespace
{
	const int64 read_timeout = 5 * 60 * 1000;
	//Maximum amount of data requested ahead of sequential reads
	const int64 readahead_max_bytes = 32 * 1024 * 1024;
	const size_t readahead_max_requests = 128;
	//Drain read-ahead if idle for this long (server times out sending responses)
	const int64 readahead_idle_drain = 5000;
	//Number of consecutive reads continuing the previous one before reads are considered sequential
	const int sequential_reads_min = 2;
}

ImageReadFile::ImageReadFile(IPipe* pipe, int64 size)
	: pipe(pipe), size(size), pos(0), mutex(Server->createMutex()),
	connection_error(false), last_request(Server->getTimeMS()),
	readahead_partial(false), readahead_bytes(0), readahead_window(0), last_read_end(-1),
	sequential_reads(0), read_sequential(false),
	last_read_time(Server->getTimeMS())
{
}

ImageReadFile::~ImageReadFile()
{
	Server->destroy(mutex);
}

std::string ImageReadFile::Read(_u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(tr);
	_u32 r = Read(&ret[0], tr, has_error);
	ret.resize(r);
	return ret;
}

std::string ImageReadFile::Read(int64 spos, _u32 tr, bool *has_error)
{
	std::string ret;
	ret.resize(tr);
	_u32 r = Read(spos, &ret[0], tr, has_error);
	ret.resize(r);
	return ret;
}

_u32 ImageReadFile::Read(char* buffer, _u32 bsize, bool *has_error)
{
	_u32 r = Read(pos, buffer, bsize, has_error);
	pos += r;
	return r;
}

_u32 ImageReadFile::Read(int64 spos, char* buffer, _u32 bsize, bool *has_error)
{
	IScopedLock lock(mutex);

	if (connection_error)
	{
		if (has_error) *has_error = true;
		return 0;
	}

	if (spos < 0 || spos >= size)
	{
		return 0;
	}

	if (bsize > size - spos)
	{
		bsize = static_cast<_u32>(size - spos);
	}

	if (spos == last_read_end)
	{
		if (sequential_reads < sequential_reads_min)
		{
			++sequential_reads;
		}
		read_sequential = sequential_reads >= sequential_reads_min;
	}
	else
	{
		sequential_reads = 0;
		read_sequential = false;
		readahead_window = 0;
	}
	last_read_end = spos + bsize;
	last_read_time = Server->getTimeMS();

	_u32 done = 0;
	while (done < bsize)
	{
		SImageReadRequest req;
		req.cmd = image_read_cmd_read;
		req.len = (std::min)(bsize - done, image_read_max_len);
		req.offset = spos + done;

		//Response to the read-ahead request is next. A read may consume only
		//the first part of it, the rest is consumed by the following read.
		const bool from_readahead = !readahead.empty()
			&& readahead.front().offset == req.offset;

		if (from_readahead)
		{
			req.len = (std::min)(req.len, readahead.front().len);
		}
		else
		{
			if (!readahead.empty())
			{
				readahead_window = 0;
				if (!drainReadahead())
				{
					if (has_error) *has_error = true;
					return done;
				}
			}

			if (!sendRequests(&req, 1))
			{
				if (has_error) *has_error = true;
				return done;
			}
		}

		if (read_sequential)
		{
			readahead_window = (std::min)(readahead_max_bytes,
				(std::max)(static_cast<int64>(req.len), readahead_window * 2));

			if (!queueReadahead(req.offset + req.len, (std::min)(bsize, image_read_max_len)))
			{
				if (has_error) *has_error = true;
				return done;
			}
		}

		if (!from_readahead || !readahead_partial)
		{
			int err;
			if (!receiveResponse(&err))
			{
				if (has_error) *has_error = true;
				return done;
			}

			if (err != image_read_err_ok)
			{
				if (from_readahead)
				{
					readahead_bytes -= readahead.front().len;
					readahead.pop_front();
				}
				Server->Log("Server returned error " + convert(err) + " reading " + convert(req.len) + " bytes at offset " + convert(req.offset), LL_ERROR);
				if (has_error) *has_error = true;
				return done;
			}
		}

		if (from_readahead)
		{
			SImageReadRequest& front = readahead.front();
			readahead_bytes -= req.len;
			if (req.len < front.len)
			{
				front.offset += req.len;
				front.len -= req.len;
				readahead_partial = true;
			}
			else
			{
				readahead.pop_front();
				readahead_partial = false;
			}
		}

		if (!image_read_full(pipe, buffer + done, req.len, read_timeout))
		{
			Server->Log("Error receiving image data", LL_ERROR);
			connection_error = true;
			if (has_error) *has_error = true;
			return done;
		}

		done += req.len;
	}

	return done;
}

_u32 ImageReadFile::Write(const std::string &tw, bool *has_error)
{
	if (has_error) *has_error = true;
	return 0;
}

_u32 ImageReadFile::Write(int64 spos, const std::string &tw, bool *has_error)
{
	if (has_error) *has_error = true;
	return 0;
}

_u32 ImageReadFile::Write(const char* buffer, _u32 bsiz, bool *has_error)
{
	if (has_error) *has_error = true;
	return 0;
}

_u32 ImageReadFile::Write(int64 spos, const char* buffer, _u32 bsiz, bool *has_error)
{
	if (has_error) *has_error = true;
	return 0;
}

bool ImageReadFile::Seek(_i64 spos)
{
	if (spos < 0 || spos > size)
	{
		return false;
	}
	pos = spos;
	return true;
}

_i64 ImageReadFile::Size(void)
{
	return size;
}

_i64 ImageReadFile::RealSize()
{
	return size;
}

bool ImageReadFile::PunchHole(_i64 spos, _i64 size)
{
	return false;
}

bool ImageReadFile::Sync()
{
	return false;
}

std::string ImageReadFile::getFilename(void)
{
	return "image backup";
}

bool ImageReadFile::keepalive()
{
	IScopedLock lock(mutex);

	if (connection_error)
	{
		return false;
	}

	if (!readahead.empty()
		&& Server->getTimeMS() - last_read_time > readahead_idle_drain)
	{
		readahead_window = 0;
		if (!drainReadahead())
		{
			return false;
		}
	}

	if (Server->getTimeMS() - last_request < image_read_keepalive_interval)
	{
		return true;
	}

	if (!readahead.empty())
	{
		//Server is still sending read-ahead responses
		return true;
	}

	SImageReadRequest req = {};
	req.cmd = image_read_cmd_keepalive;
	int err;
	return request(req, &err);
}

void ImageReadFile::close()
{
	IScopedLock lock(mutex);

	if (connection_error)
	{
		return;
	}

	if (!drainReadahead())
	{
		return;
	}

	SImageReadRequest req = {};
	req.cmd = image_read_cmd_close;
	char req_buf[image_read_request_size];
	image_read_encode_request(req, req_buf);
	pipe->Write(req_buf, sizeof(req_buf), 30000);
	connection_error = true;
}

bool ImageReadFile::request(const SImageReadRequest& req, int* err)
{
	return sendRequests(&req, 1)
		&& receiveResponse(err);
}

bool ImageReadFile::sendRequests(const SImageReadRequest* reqs, size_t n)
{
	std::vector<char> req_buf(image_read_request_size * n);
	for (size_t i = 0; i < n; ++i)
	{
		image_read_encode_request(reqs[i], &req_buf[i * image_read_request_size]);
	}

	if (!pipe->Write(req_buf.data(), req_buf.size(), 30000))
	{
		Server->Log("Error sending image read request", LL_ERROR);
		connection_error = true;
		return false;
	}

	last_request = Server->getTimeMS();
	return true;
}

bool ImageReadFile::receiveResponse(int* err)
{
	char resp_buf[image_read_response_header_size];
	if (!image_read_full(pipe, resp_buf, sizeof(resp_buf), read_timeout))
	{
		Server->Log("Error receiving image read response", LL_ERROR);
		connection_error = true;
		return false;
	}

	memcpy(err, resp_buf, sizeof(*err));
	*err = little_endian(*err);
	return true;
}

bool ImageReadFile::queueReadahead(int64 read_end, _u32 len)
{
	int64 next_offset = readahead.empty() ? read_end
		: static_cast<int64>(readahead.back().offset) + readahead.back().len;

	std::vector<SImageReadRequest> reqs;
	while (next_offset < size
		&& readahead_bytes + len <= readahead_window
		&& readahead.size() < readahead_max_requests)
	{
		SImageReadRequest req;
		req.cmd = image_read_cmd_read;
		req.len = static_cast<_u32>((std::min)(static_cast<int64>(len), size - next_offset));
		req.offset = next_offset;
		reqs.push_back(req);
		readahead.push_back(req);
		readahead_bytes += req.len;
		next_offset += req.len;
	}

	if (reqs.empty())
	{
		return true;
	}

	return sendRequests(reqs.data(), reqs.size());
}

bool ImageReadFile::drainReadahead()
{
	std::vector<char> buf;
	while (!readahead.empty())
	{
		SImageReadRequest req = readahead.front();
		readahead.pop_front();
		readahead_bytes -= req.len;

		if (readahead_partial)
		{
			//Response header was already received
			readahead_partial = false;
		}
		else
		{
			int err;
			if (!receiveResponse(&err))
			{
				return false;
			}

			if (err != image_read_err_ok)
			{
				continue;
			}
		}

		buf.resize(req.len);
		if (!image_read_full(pipe, buf.data(), req.len, read_timeout))
		{
			Server->Log("Error receiving read-ahead image data", LL_ERROR);
			connection_error = true;
			return false;
		}
	}
	return true;
}
