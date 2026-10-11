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

namespace
{
	const int64 read_timeout = 5 * 60 * 1000;
}

ImageReadFile::ImageReadFile(IPipe* pipe, int64 size)
	: pipe(pipe), size(size), pos(0), mutex(Server->createMutex()),
	connection_error(false), last_request(Server->getTimeMS())
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

	_u32 done = 0;
	while (done < bsize)
	{
		SImageReadRequest req;
		req.cmd = image_read_cmd_read;
		req.len = (std::min)(bsize - done, image_read_max_len);
		req.offset = spos + done;

		int err;
		if (!request(req, &err))
		{
			if (has_error) *has_error = true;
			return done;
		}

		if (err != image_read_err_ok)
		{
			Server->Log("Server returned error " + convert(err) + " reading " + convert(req.len) + " bytes at offset " + convert(req.offset), LL_ERROR);
			if (has_error) *has_error = true;
			return done;
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

	if (Server->getTimeMS() - last_request < image_read_keepalive_interval)
	{
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

	SImageReadRequest req = {};
	req.cmd = image_read_cmd_close;
	char req_buf[image_read_request_size];
	image_read_encode_request(req, req_buf);
	pipe->Write(req_buf, sizeof(req_buf), 30000);
	connection_error = true;
}

bool ImageReadFile::request(const SImageReadRequest& req, int* err)
{
	char req_buf[image_read_request_size];
	image_read_encode_request(req, req_buf);
	if (!pipe->Write(req_buf, sizeof(req_buf), 30000))
	{
		Server->Log("Error sending image read request", LL_ERROR);
		connection_error = true;
		return false;
	}

	last_request = Server->getTimeMS();

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
