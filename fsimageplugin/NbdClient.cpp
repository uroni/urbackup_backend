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

const uint64 NBD_OPTS_MAGIC = 0x49484156454F5054ULL; //IHAVEOPT
const uint64 NBD_REP_MAGIC = 0x3e889045565a9ULL;

const unsigned short NBD_FLAG_FIXED_NEWSTYLE = 1<<0;
const unsigned short NBD_FLAG_NO_ZEROES = 1<<1;
const unsigned int NBD_FLAG_C_FIXED_NEWSTYLE = 1<<0;
const unsigned int NBD_FLAG_C_NO_ZEROES = 1<<1;

//NBD_FLAG_HAS_FLAGS|NBD_FLAG_SEND_FLUSH|NBD_FLAG_ROTATIONAL|NBD_FLAG_SEND_TRIM
const unsigned short NBD_TRANSMISSION_FLAGS = (1<<0) | (1<<2) | (1<<4) | (1<<5);

const unsigned int NBD_OPT_EXPORT_NAME = 1;
const unsigned int NBD_OPT_ABORT = 2;
const unsigned int NBD_OPT_LIST = 3;
const unsigned int NBD_OPT_INFO = 6;
const unsigned int NBD_OPT_GO = 7;

const unsigned int NBD_REP_ACK = 1;
const unsigned int NBD_REP_SERVER = 2;
const unsigned int NBD_REP_INFO = 3;
const unsigned int NBD_REP_ERR_UNSUP = 0x80000001;
const unsigned int NBD_REP_ERR_INVALID = 0x80000003;

const unsigned short NBD_INFO_EXPORT = 0;

const size_t max_option_size = 64*1024;

NbdClient::NbdClient( NbdService* service, IFile* backing_file )
	: service(service), was_client(false), no_zeroes(false), backing_file(backing_file)
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
		char msg[8+8+2];
		memcpy(&msg[0], "NBDMAGIC", 8);
		uint64 opts_magic = big_endian(NBD_OPTS_MAGIC);
		memcpy(&msg[8], &opts_magic, sizeof(opts_magic));
		unsigned short handshake_flags = big_endian(static_cast<unsigned short>(NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES));
		memcpy(&msg[16], &handshake_flags, sizeof(handshake_flags));

		if(!pipe->Write(msg, sizeof(msg)))
		{
			Server->Log("Error in handshake", LL_ERROR);
			shutdown=true;
			return -1;
		}

		state=ClientState_ClientFlags;
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

	while(!shutdown)
	{
		bool cont;
		if(state==ClientState_ClientFlags)
		{
			cont = handleClientFlags();
		}
		else if(state==ClientState_Options)
		{
			cont = handleOption();
		}
		else
		{
			cont = recv_buf_off>=header_size && handleRequests();
		}

		if(!cont)
		{
			break;
		}
	}
}

void NbdClient::consumeRecvBuf(size_t n)
{
	if(n<recv_buf_off)
	{
		memmove(&recv_buf[0], &recv_buf[n], recv_buf_off-n);
	}
	recv_buf_off-=n;
}

bool NbdClient::handleClientFlags()
{
	if(recv_buf_off<sizeof(unsigned int))
	{
		return false;
	}

	unsigned int client_flags;
	memcpy(&client_flags, &recv_buf[0], sizeof(client_flags));
	client_flags = big_endian(client_flags);

	if(client_flags & ~(NBD_FLAG_C_FIXED_NEWSTYLE | NBD_FLAG_C_NO_ZEROES))
	{
		Server->Log("Unknown NBD client flags "+convert(client_flags), LL_ERROR);
		shutdown=true;
		return false;
	}

	no_zeroes = (client_flags & NBD_FLAG_C_NO_ZEROES)!=0;

	consumeRecvBuf(sizeof(unsigned int));
	state=ClientState_Options;
	return true;
}

bool NbdClient::sendOptionReply(unsigned int opt, unsigned int reply_type, const char* data, size_t data_size)
{
	std::vector<char> msg(8+4+4+4+data_size);
	uint64 magic = big_endian(NBD_REP_MAGIC);
	memcpy(&msg[0], &magic, sizeof(magic));
	opt = big_endian(opt);
	memcpy(&msg[8], &opt, sizeof(opt));
	reply_type = big_endian(reply_type);
	memcpy(&msg[12], &reply_type, sizeof(reply_type));
	unsigned int len = big_endian(static_cast<unsigned int>(data_size));
	memcpy(&msg[16], &len, sizeof(len));
	if(data_size>0)
	{
		memcpy(&msg[20], data, data_size);
	}

	if(!pipe->Write(msg.data(), msg.size()))
	{
		Server->Log("Error sending NBD option reply", LL_ERROR);
		shutdown=true;
		return false;
	}
	return true;
}

bool NbdClient::sendExportInfo(unsigned int opt)
{
	char info[2+8+2];
	unsigned short info_type = big_endian(static_cast<unsigned short>(NBD_INFO_EXPORT));
	memcpy(&info[0], &info_type, sizeof(info_type));
	uint64 size = big_endian(static_cast<uint64>(backing_file->Size()));
	memcpy(&info[2], &size, sizeof(size));
	unsigned short transmission_flags = big_endian(NBD_TRANSMISSION_FLAGS);
	memcpy(&info[10], &transmission_flags, sizeof(transmission_flags));

	return sendOptionReply(opt, NBD_REP_INFO, info, sizeof(info));
}

bool NbdClient::handleOption()
{
	const size_t opt_header_size = 8+4+4;

	if(recv_buf_off<opt_header_size)
	{
		return false;
	}

	uint64 magic;
	memcpy(&magic, &recv_buf[0], sizeof(magic));
	if(magic!=big_endian(NBD_OPTS_MAGIC))
	{
		Server->Log("Client option magic wrong!", LL_ERROR);
		shutdown=true;
		return false;
	}

	unsigned int opt;
	memcpy(&opt, &recv_buf[8], sizeof(opt));
	opt = big_endian(opt);
	unsigned int len;
	memcpy(&len, &recv_buf[12], sizeof(len));
	len = big_endian(len);

	if(len>max_option_size)
	{
		Server->Log("NBD option "+convert(opt)+" too large ("+convert(len)+" bytes)", LL_ERROR);
		shutdown=true;
		return false;
	}

	if(recv_buf_off<opt_header_size+len)
	{
		if(opt_header_size+len>recv_buf.size())
		{
			recv_buf.resize(opt_header_size+len);
		}
		return false;
	}

	const char* data = &recv_buf[opt_header_size];

	switch(opt)
	{
	case NBD_OPT_EXPORT_NAME:
		{
			std::vector<char> msg(8+2+(no_zeroes ? 0 : 124));
			uint64 size = big_endian(static_cast<uint64>(backing_file->Size()));
			memcpy(&msg[0], &size, sizeof(size));
			unsigned short transmission_flags = big_endian(NBD_TRANSMISSION_FLAGS);
			memcpy(&msg[8], &transmission_flags, sizeof(transmission_flags));

			if(!pipe->Write(msg.data(), msg.size()))
			{
				Server->Log("Error sending NBD export info", LL_ERROR);
				shutdown=true;
				return false;
			}
			state=ClientState_Data;
		} break;
	case NBD_OPT_ABORT:
		{
			sendOptionReply(opt, NBD_REP_ACK, NULL, 0);
			shutdown=true;
		} break;
	case NBD_OPT_LIST:
		{
			//Single export with empty name
			char server[4] = {};
			if(sendOptionReply(opt, NBD_REP_SERVER, server, sizeof(server)))
			{
				sendOptionReply(opt, NBD_REP_ACK, NULL, 0);
			}
		} break;
	case NBD_OPT_INFO:
	case NBD_OPT_GO:
		{
			//Any export name is accepted. Information requests are answered with NBD_INFO_EXPORT only.
			unsigned int name_len=0;
			unsigned short n_requests=0;
			if(len>=4)
			{
				memcpy(&name_len, data, sizeof(name_len));
				name_len = big_endian(name_len);
			}
			if(len>=4+2 && name_len<=len-4-2)
			{
				memcpy(&n_requests, data+4+name_len, sizeof(n_requests));
				n_requests = big_endian(n_requests);
			}
			if(len<4+2 || name_len>len-4-2
				|| len!=4+name_len+2+2*static_cast<size_t>(n_requests))
			{
				sendOptionReply(opt, NBD_REP_ERR_INVALID, NULL, 0);
				break;
			}

			if(sendExportInfo(opt)
				&& sendOptionReply(opt, NBD_REP_ACK, NULL, 0)
				&& opt==NBD_OPT_GO)
			{
				state=ClientState_Data;
			}
		} break;
	default:
		{
			Server->Log("Unsupported NBD option "+convert(opt), LL_DEBUG);
			sendOptionReply(opt, NBD_REP_ERR_UNSUP, NULL, 0);
		} break;
	}

	consumeRecvBuf(opt_header_size+len);

	return !shutdown;
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
	//Upper 16 bits are command flags
	type = big_endian(type) & 0xffff;
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
