#include "NbdClient.h"
#include "NbdService.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <memory.h>
#include <assert.h>
#include <algorithm>

const unsigned int NBD_CMD_READ = 0;
const unsigned int NBD_CMD_WRITE = 1;
const unsigned int NBD_CMD_DISC = 2;
const unsigned int NBD_CMD_FLUSH = 3;
const unsigned int NBD_CMD_TRIM = 4;

NbdClient::NbdClient( NbdService* service, IFile* backing_file )
	: service(service), was_client(false), backing_file(backing_file)
{

}

NbdClient::~NbdClient()
{
	if(was_client)
	{
		service->releaseClient();
	}
}

void NbdClient::Init( THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName, const IClientWakeup* wakeup )
{
	shutdown=false;
	pipe=pPipe;
	state=ClientState_Handshake;
	recv_buf_off=0;
	recv_buf.resize(4096*2);
	response_buf.resize(4096*2);

	if(!service->acquireClient())
	{
		Server->Log("Already has client. Rejecting connection.", LL_WARNING);
		shutdown=true;
		was_client=false;
	}
	else
	{
		Server->Log("Client connected");
		was_client=true;
	}
}

int NbdClient::Run()
{
	if(shutdown)
	{
		return -1;
	}

	if(state==ClientState_Handshake)
	{
		std::vector<char> msg;
		msg.resize(8+8+8+4+124);
		memcpy(&msg[0], "NBDMAGIC", 8);
		uint64 magic2 = big_endian(0x00420281861253ULL);
		memcpy(&msg[8], &magic2, sizeof(magic2));
		uint64 size = big_endian(static_cast<uint64>(backing_file->Size()));
		memcpy(&msg[16], &size, sizeof(size));
		unsigned int flags = 1 & (1<<2) & (1<<4) & (1<<5); //NBD_FLAG_HAS_FLAGS|NBD_FLAG_SEND_FLUSH|NBD_FLAG_ROTATIONAL|NBD_FLAG_SEND_TRIM
		memcpy(&msg[24], &flags, sizeof(flags));

		if(!pipe->Write(msg.data(), msg.size()))
		{
			Server->Log("Error in handshake", LL_ERROR);
			shutdown=true;
			return -1;
		}

		state=ClientState_Data;
	}

	return 1000;
}

void NbdClient::ReceivePackets()
{
	if(state==ClientState_Handshake)
	{
		return;
	}

	size_t read = pipe->Read(&recv_buf[recv_buf_off], (std::min)((size_t)2*4096, recv_buf.size()-recv_buf_off) );

	if(read==0)
	{
		Server->Log("Nothing to read. Shutting down.");
		shutdown=true;
		return;
	}

	recv_buf_off+=read;

	const size_t header_size = 4+4+8+8+4;

	while(recv_buf_off>=header_size)
	{
		if(!handleRequests())
		{
			break;
		}
	}
}

bool NbdClient::handleRequests()
{
	const size_t header_size = 4+4+8+8+4;

	unsigned int magic;
	memcpy(&magic, &recv_buf[0], sizeof(magic));

	if(magic!=big_endian(0x25609513U))
	{
		Server->Log("Client magic wrong!", LL_ERROR);
		shutdown=true;
		return false;
	}

	unsigned int type;
	memcpy(&type, &recv_buf[4], sizeof(type));
	type = big_endian(type);
	int64 handle;
	memcpy(&handle, &recv_buf[8], sizeof(int64));
	int64 offset;
	memcpy(&offset, &recv_buf[16], sizeof(offset));
	offset = big_endian(offset);
	unsigned int len;
	memcpy(&len, &recv_buf[24], sizeof(len));
	len = big_endian(len);

	size_t used_bytes=header_size;

	if(type==NBD_CMD_WRITE
		&& recv_buf_off<header_size+len)
	{
		Server->Log("Not enough data for write. Retrying... (need "+convert(header_size+len-recv_buf_off)+" bytes more)", LL_DEBUG);
		if(header_size+len>recv_buf.size())
		{
			recv_buf.resize(header_size+len);
		}
		return false;
	}
	else if(type==NBD_CMD_WRITE)
	{
		used_bytes+=len;
	}

	if(type==NBD_CMD_WRITE || type==NBD_CMD_READ)
	{
		if(!backing_file->Seek(offset))
		{
			Server->Log("Error seeking to offset "+convert(offset)+" in backing file", LL_ERROR);
			shutdown=true;
			return false;
		}
	}

	int err=0;

	const size_t response_header_size = 4+4+8;

	if(type==NBD_CMD_READ
		&& response_header_size+len>response_buf.size())
	{
		response_buf.resize(response_header_size+len);
	}

	unsigned int response_magic = big_endian(0x67446698U);
	memcpy(&response_buf[0], &response_magic, sizeof(response_magic));
	memcpy(&response_buf[8], &handle, sizeof(handle));

	size_t response_size = response_header_size;

	switch(type)
	{
	case NBD_CMD_WRITE:
		{
			if(backing_file->Write(&recv_buf[header_size], len)!=len)
			{
				Server->Log("Error writing "+convert(len)+" bytes at offset "+convert(offset)+" to backing file", LL_ERROR);
				err=5;
			}
		} break;
	case NBD_CMD_READ:
		{
			size_t read = backing_file->Read(&response_buf[response_header_size], len);

			if(read!=len)
			{
				Server->Log("Error reading "+convert(len)+" bytes at offset "+convert(offset)+" from backing file", LL_ERROR);
				err=5;
			}
			else
			{
				response_size+=len;
			}
		} break;
	case NBD_CMD_TRIM:
		{
			//TODO
		} break;
	case NBD_CMD_DISC:
		{
			shutdown=true;
		}break;
	case NBD_CMD_FLUSH:
		{
			Server->Log("Client requested flush");
			if(!backing_file->Sync())
			{
				Server->Log("Error syncing backing file", LL_ERROR);
				err=5;
			}
		} break;
	default:
		{
			Server->Log("Unkown command", LL_ERROR);
			shutdown=true;
			assert(false);
		} break;
	}

	err=big_endian(err);
	memcpy(&response_buf[4], &err, sizeof(err));

	if(!pipe->Write(response_buf.data(), response_size))
	{
		Server->Log("Error sending response of size "+convert(response_size)+" to NBD client", LL_ERROR);
		shutdown=true;
		return false;
	}

	if(used_bytes<recv_buf_off)
	{
		Server->Log(convert(recv_buf_off-used_bytes)+" extra bytes in recv_buf. Moving...", LL_DEBUG);
		memmove(&recv_buf[0], &recv_buf[used_bytes], recv_buf_off-used_bytes);
	}
	recv_buf_off-=used_bytes;

	return true;
}
