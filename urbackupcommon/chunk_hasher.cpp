#include "chunk_hasher.h"
#include "sha2/sha2.h"
#include "../stringtools.h"
#include "../fileservplugin/chunk_settings.h"
#include "../md5.h"
#include "../common/adler32.h"
#include <memory.h>

std::string build_chunk_hashs(IFile *f, IFile *hashoutput, INotEnoughSpaceCallback *cb, bool ret_sha2, IFile *copy, bool modify_inplace, int64* inplace_written)
{
	f->Seek(0);

	hashoutput->Seek(0);
	_i64 fsize=f->Size();
	_i64 fsize_endian = little_endian(fsize);
	if(!writeRepeatFreeSpace(hashoutput, (char*)&fsize_endian, sizeof(_i64), cb))
		return "";

	sha512_ctx ctx;
	if(ret_sha2)
		sha512_init(&ctx);

	_i64 n_chunks=c_checkpoint_dist/c_small_hash_dist;
	char buf[c_small_hash_dist];
	char copy_buf[c_small_hash_dist];
	_i64 copy_write_pos=0;
	char zbuf[big_hash_size]={};
	_i64 hashoutputpos=sizeof(_i64);
	for(_i64 pos=0;pos<fsize;)
	{
		_i64 epos=pos+c_checkpoint_dist;
		MD5 big_hash;
		_i64 hashoutputpos_start=hashoutputpos;
		writeRepeatFreeSpace(hashoutput, zbuf, big_hash_size, cb);
		hashoutputpos+=big_hash_size;
		for(;pos<epos && pos<fsize;pos+=c_small_hash_dist)
		{
			_u32 r=f->Read(buf, c_small_hash_dist);
			_u32 small_hash=urb_adler32(urb_adler32(0, NULL, 0), buf, r);
			big_hash.update((unsigned char*)buf, r);
			small_hash = little_endian(small_hash);
			if(!writeRepeatFreeSpace(hashoutput, (char*)&small_hash, small_hash_size, cb))
				return "";

			hashoutputpos+=small_hash_size;

			if(ret_sha2)
			{
				sha512_update(&ctx, (unsigned char*)buf, r);
			}
			if(copy!=NULL)
			{
				if(modify_inplace)
				{
					_u32 copy_r=copy->Read(copy_buf, c_small_hash_dist);

					if(copy_r!=r || memcmp(copy_buf, buf, r)!=0)
					{
						copy->Seek(copy_write_pos);
						if(!writeRepeatFreeSpace(copy, buf, r, cb) )
							return "";

						if(inplace_written!=NULL)
						{
							*inplace_written+=r;
						}
					}

					copy_write_pos+=r;
				}
				else
				{
					if(!writeRepeatFreeSpace(copy, buf, r, cb) )
						return "";
				}
			}
		}
		hashoutput->Seek(hashoutputpos_start);
		big_hash.finalize();
		if(!writeRepeatFreeSpace(hashoutput, (const char*)big_hash.raw_digest_int(),  big_hash_size, cb))
			return "";

		hashoutput->Seek(hashoutputpos);
	}

	if(ret_sha2)
	{
		std::string ret;
		ret.resize(64);
		sha512_final(&ctx, (unsigned char*)&ret[0]);
		return ret;
	}
	else
	{
		return "k";
	}
}

bool writeRepeatFreeSpace(IFile *f, const char *buf, size_t bsize, INotEnoughSpaceCallback *cb)
{
	if( cb==NULL)
		return writeFileRepeatTries(f, buf, bsize);

	int rc=f->Write(buf, (_u32)bsize);
	if(rc!=bsize)
	{
		if(cb!=NULL && cb->handle_not_enough_space(f->getFilenameW()) )
		{
			_u32 written=rc;
			do
			{
				rc=f->Write(buf+written, (_u32)bsize-written);
				written+=rc;
			}
			while(written<bsize && rc>0);

			if(rc==0) return false;
		}
		else
		{
			return false;
		}
	}
	return true;
}

bool writeFileRepeatTries(IFile *f, const char *buf, size_t bsize)
{
	_u32 written=0;
	_u32 rc;
	int tries=50;
	do
	{
		rc=f->Write(buf+written, (_u32)(bsize-written));
		written+=rc;
		if(rc==0)
		{
			Server->wait(10000);
			--tries;
		}
	}
	while(written<bsize && (rc>0 || tries>0) );

	if(rc==0)
	{
		return false;
	}

	return true;
}