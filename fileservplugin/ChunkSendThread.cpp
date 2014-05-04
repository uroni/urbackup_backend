#include "ChunkSendThread.h"
#include "CClientThread.h"
#include "packet_ids.h"
#include "log.h"
#include "../stringtools.h"
#include "FileServ.h"
#include "socket_header.h"
#include <memory.h>

#include "../Interface/File.h"
#include "../Interface/Server.h"

unsigned int adler32(unsigned int adler, const char *buf, unsigned int len);


ChunkSendThread::ChunkSendThread(CClientThread *parent)
	: parent(parent), file(NULL)
{
	chunk_buf=new char[(c_checkpoint_dist/c_chunk_size)*(c_chunk_size)+c_chunk_padding];
}

ChunkSendThread::~ChunkSendThread(void)
{
	delete []chunk_buf;
}

void ChunkSendThread::operator()(void)
{
	SChunk chunk;
	while(parent->getNextChunk(&chunk))
	{
		if(chunk.msg != ID_ILLEGAL)
		{
			parent->SendInt(reinterpret_cast<char*>(&chunk.msg), 1);
		}
		else if(chunk.update_file!=NULL)
		{
			if(file!=NULL)
			{
				Server->destroy(file);
			}
			file=chunk.update_file;
			curr_hash_size=chunk.hashsize;
			curr_file_size=chunk.startpos;

			CWData sdata;
			sdata.addUChar(ID_FILESIZE);
			sdata.addUInt64(little_endian(curr_file_size));
			parent->SendInt(sdata.getDataPtr(), sdata.getDataSize());
		}
		else
		{
			if( FileServ::isPause() )
			{
				Sleep(500);
			}
			sendChunk(&chunk);
		}
	}
	if(file!=NULL)
	{
		Server->destroy(file);
		file=NULL;
	}
	delete this;
}

bool ChunkSendThread::sendChunk(SChunk *chunk)
{
	file->Seek(chunk->startpos);

	if(chunk->transfer_all)
	{
		size_t off=1+sizeof(_i64)+sizeof(_u32);
		*chunk_buf=ID_WHOLE_BLOCK;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));

		unsigned int blockleft;
		if(curr_file_size<=chunk->startpos)
		{
			blockleft=0;
		}
		else if(curr_file_size-chunk->startpos<c_checkpoint_dist)
		{
			blockleft=static_cast<unsigned int>(curr_file_size-chunk->startpos);
		}
		else
		{
			blockleft=c_checkpoint_dist;
		}

		md5_hash.init();

		unsigned tmp_blockleft=little_endian(blockleft);
		memcpy(chunk_buf+1+sizeof(_i64), &tmp_blockleft, sizeof(unsigned int));

		Log("Sending whole block start="+nconvert(chunk->startpos)+" size="+nconvert(blockleft), LL_DEBUG);
		_u32 r;

		do
		{
			r=0;
			if(blockleft>0)
			{
				r=file->Read(chunk_buf+off, (std::min)(blockleft, c_chunk_size) );
			}

			if(r>0)
			{
				md5_hash.update((unsigned char*)chunk_buf+off, r);
			}
			if(r+off>0)
			{
				if(parent->SendInt(chunk_buf, off+r)==SOCKET_ERROR)
					return false;

				if( FileServ::isPause() ) Sleep(500);

				off=0;
			}

			if(r<=blockleft)
				blockleft-=r;
			else
				blockleft=0;

		}
		while(r==c_chunk_size && blockleft>0);	

		md5_hash.finalize();
		*chunk_buf=ID_BLOCK_HASH;
		chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);

		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
			return false;

		if( FileServ::isPause() ) Sleep(500);

		return true;
	}
	unsigned int next_smallhash=c_small_hash_dist;

	unsigned int read_total=0;
	_u32 r=0;
	bool sent_update=false;
	char* cptr=chunk_buf+c_chunk_padding;
	_i64 curr_pos=chunk->startpos;
	unsigned int c_adler=adler32(0, NULL, 0);
	md5_hash.init();
	unsigned int small_hash_num=0;
	do
	{
		cptr+=r;

		_u32 to_read = c_chunk_size;

		if(curr_file_size<=curr_pos)
		{
			to_read = 0;
		}
		else if(curr_file_size-curr_pos<to_read)
		{
			to_read = static_cast<_u32>(curr_file_size-curr_pos);
		}

		r=file->Read(cptr, to_read);

		if(r>0)
		{
			md5_hash.update((unsigned char*)cptr, (unsigned int)r);
			c_adler=adler32(c_adler, cptr, r);

			read_total+=r;

			if(read_total==next_smallhash || r!=c_chunk_size)
			{
				if(c_adler!=little_endian(*((_u32*)&chunk->small_hash[small_hash_size*small_hash_num]))
					|| curr_pos+r>curr_hash_size)
				{
					sent_update=true;
					char tmp_backup[c_chunk_padding];
					memcpy(tmp_backup, cptr-c_chunk_padding, c_chunk_padding);

					*(cptr-c_chunk_padding)=ID_UPDATE_CHUNK;
					_i64 curr_pos_tmp = little_endian(curr_pos);
					memcpy(cptr-sizeof(_i64)-sizeof(_u32), &curr_pos_tmp, sizeof(_i64));
					_u32 r_tmp = little_endian(r);
					memcpy(cptr-sizeof(_u32), &r_tmp, sizeof(_u32));

					Log("Sending chunk start="+nconvert(curr_pos)+" size="+nconvert(r), LL_DEBUG);

					if(parent->SendInt(cptr-c_chunk_padding, c_chunk_padding+r)==SOCKET_ERROR)
						return false;

					if( FileServ::isPause() ) Sleep(500);

					memcpy(cptr-c_chunk_padding, tmp_backup, c_chunk_padding);
				}

				c_adler=adler32(0, NULL, 0);
				++small_hash_num;
				next_smallhash+=c_small_hash_dist;
			}
			curr_pos+=r;
		}
	}while(r==c_chunk_size && read_total<c_checkpoint_dist);

	md5_hash.finalize();

	if(!sent_update && memcmp(md5_hash.raw_digest_int(), chunk->big_hash, big_hash_size)!=0 )
	{
		Log("Sending whole block(2) start="+nconvert(chunk->startpos)+" size="+nconvert(read_total), LL_DEBUG);

		*chunk_buf=ID_WHOLE_BLOCK;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		unsigned int read_total_tmp = little_endian(read_total);
		memcpy(chunk_buf+1+sizeof(_i64), &read_total_tmp, sizeof(_u32));
		if(parent->SendInt(chunk_buf, read_total+1+sizeof(_i64)+sizeof(_u32))==SOCKET_ERROR)
			return false;

		if( FileServ::isPause() ) Sleep(500);

		*chunk_buf=ID_BLOCK_HASH;
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
			return false;

		if( FileServ::isPause() ) Sleep(500);
	}
	else if(!sent_update)
	{
		*chunk_buf=ID_NO_CHANGE;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64))==SOCKET_ERROR)
			return false;

		if( FileServ::isPause() ) Sleep(500);
	}
	else
	{
		*chunk_buf=ID_BLOCK_HASH;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
			return false;

		if( FileServ::isPause() ) Sleep(500);
	}

	return true;
}