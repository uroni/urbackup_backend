#include "ChunkSendThread.h"
#include "CClientThread.h"
#include "packet_ids.h"

#include "../Interface/File.h"
#include "../Interface/Server.h"

unsigned int adler32(unsigned int adler, const char *buf, unsigned int len);


ChunkSendThread::ChunkSendThread(CClientThread *parent, IFile *file)
	: parent(parent), file(file)
{
	chunk_buf=new char[(c_checkpoint_dist/c_chunk_size)*(c_chunk_size+c_chunk_padding)];
}

void ChunkSendThread::operator()(void)
{
	SChunk chunk;
	while(parent->getNextChunk(&chunk))
	{
		md5_hash.init();
		sendChunk(&chunk);
	}
	Server->destroy(file);
	delete this;
}

bool ChunkSendThread::sendChunk(SChunk *chunk)
{
	file->Seek(chunk->startpos);

	if(chunk->transfer_all)
	{
		size_t off=1+sizeof(_i64)+sizeof(_u32);
		*chunk_buf=ID_WHOLE_BLOCK;
		memcpy(chunk_buf+1, &chunk->startpos, sizeof(_i64));

		unsigned int blockleft;
		if(file->Size()-chunk->startpos<c_checkpoint_dist)
			blockleft=(unsigned int)(file->Size()-chunk->startpos);
		else
			blockleft=c_checkpoint_dist;

		memcpy(chunk_buf+1+sizeof(_i64), &blockleft, sizeof(unsigned int));
		_u32 r;
		do
		{
			r=file->Read(chunk_buf+off, c_chunk_size);
			if(r+off>0)
			{
				if(parent->SendInt(chunk_buf, off+r)==SOCKET_ERROR)
					return false;

				off=0;
			}
		}while(r==c_chunk_size);

		return true;
	}
	unsigned int next_smallhash=c_small_hash_dist;

	unsigned int read_total=0;
	_u32 r=0;
	bool sent_update=false;
	char* cptr=chunk_buf;
	_i64 curr_pos=chunk->startpos;
	unsigned int c_adler=adler32(0, NULL, 0);
	unsigned int small_hash_num=0;
	do
	{
		cptr+=r+c_chunk_padding;

		r=file->Read(cptr, c_chunk_size);

		md5_hash.update((unsigned char*)cptr, (unsigned int)r);
		c_adler=adler32(c_adler, cptr, r);

		read_total+=r;

		if(read_total==next_smallhash)
		{
			if(c_adler!=*((_u32*)&chunk->small_hash[small_hash_size*small_hash_num]))
			{
				sent_update=true;
				*(cptr-c_chunk_padding)=ID_UPDATE_CHUNK;
				memcpy(cptr-sizeof(_i64)-sizeof(_u32), &curr_pos, sizeof(_i64));
				memcpy(cptr-sizeof(_u32), &r, sizeof(_u32));
				if(parent->SendInt(cptr-c_chunk_padding, c_chunk_padding+r)==SOCKET_ERROR)
					return false;
			}

			c_adler=adler32(0, NULL, 0);
			++small_hash_num;
			next_smallhash+=c_small_hash_dist;
		}
		curr_pos+=r;

	}while(r==c_chunk_size);

	md5_hash.finalize();

	if(!sent_update && memcmp(md5_hash.raw_digest_int(), chunk->big_hash, big_hash_size)!=0 )
	{
		*chunk_buf=ID_WHOLE_BLOCK;
		memcpy(chunk_buf+1, &chunk->startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), &read_total, sizeof(_u32));
		if(parent->SendInt(chunk_buf, read_total+1+sizeof(_i64)+sizeof(_u32))==SOCKET_ERROR)
			return false;

		*chunk_buf=ID_BLOCK_HASH;
		memcpy(chunk_buf+1, &chunk->startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
			return false;
	}
	else if(!sent_update)
	{
		*chunk_buf=ID_NO_CHANGE;
		memcpy(chunk_buf+1, &chunk->startpos, sizeof(_i64));
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64))==SOCKET_ERROR)
			return false;
	}
	else
	{
		*chunk_buf=ID_BLOCK_HASH;
		memcpy(chunk_buf+1, &chunk->startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
			return false;
	}

	return true;
}