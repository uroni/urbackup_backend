#pragma once
#include "../Interface/Types.h"
#include "../Interface/Pipe.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <memory.h>

/*
* Random access read protocol for image backups ("READ IMAGE"). Used to serve an
* image backup via NBD on the client, with the reads being sent over the channel.
*
* After the "READ IMAGE" command the server answers with the volume size (int64,
* little endian), or -1 on error. Afterwards the client sends requests and the
* server answers each one, in order:
*
* Request (all little endian):
*   uint32 cmd (image_read_cmd_*)
*   uint32 len
*   uint64 offset
*
* Response:
*   int32 err (0 on success, image_read_resp_zero, otherwise NBD style errno)
*   len bytes of data if cmd==image_read_cmd_read and err==0
*   no data if err==image_read_resp_zero (no block of the range is present in the
*   image, the range reads as zeroes)
*
* image_read_cmd_close has no response.
*/

const unsigned int image_read_cmd_read = 0;
const unsigned int image_read_cmd_keepalive = 1;
const unsigned int image_read_cmd_close = 2;

const size_t image_read_request_size = 4 + 4 + 8;
const size_t image_read_response_header_size = 4;

const unsigned int image_read_max_len = 4 * 1024 * 1024;

const int image_read_err_ok = 0;
const int image_read_err_io = 5;
const int image_read_err_inval = 22;
//Success, but the range is not present in the image. No data follows, the range reads as zeroes
const int image_read_resp_zero = -1;

//Client sends keepalive if idle for this long
const int64 image_read_keepalive_interval = 60000;
//Server and relay time out if no request was received for this long
const int64 image_read_idle_timeout = 10 * 60 * 1000;

struct SImageReadRequest
{
	unsigned int cmd;
	unsigned int len;
	uint64 offset;
};

inline void image_read_encode_request(const SImageReadRequest& req, char* buf)
{
	unsigned int cmd = little_endian(req.cmd);
	unsigned int len = little_endian(req.len);
	uint64 offset = little_endian(req.offset);
	memcpy(buf, &cmd, sizeof(cmd));
	memcpy(buf + 4, &len, sizeof(len));
	memcpy(buf + 8, &offset, sizeof(offset));
}

inline SImageReadRequest image_read_decode_request(const char* buf)
{
	SImageReadRequest req;
	memcpy(&req.cmd, buf, sizeof(req.cmd));
	memcpy(&req.len, buf + 4, sizeof(req.len));
	memcpy(&req.offset, buf + 8, sizeof(req.offset));
	req.cmd = little_endian(req.cmd);
	req.len = little_endian(req.len);
	req.offset = little_endian(req.offset);
	return req;
}

//Reads exactly bsize bytes. Fails if no data was received for timeoutms.
inline bool image_read_full(IPipe* pipe, char* buf, size_t bsize, int64 timeoutms)
{
	size_t off = 0;
	int64 last_data = Server->getTimeMS();
	while (off < bsize)
	{
		size_t r = pipe->Read(buf + off, bsize - off, 1000);
		if (r == 0)
		{
			if (pipe->hasError()
				|| Server->getTimeMS() - last_data > timeoutms)
			{
				return false;
			}
			continue;
		}
		off += r;
		last_data = Server->getTimeMS();
	}
	return true;
}
