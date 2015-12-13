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

#include "chunk_hasher.h"
#include "sha2/sha2.h"
#include "../stringtools.h"
#include "../fileservplugin/chunk_settings.h"
#include "../md5.h"
#include "../common/adler32.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include <memory.h>
#include <memory>

std::string build_chunk_hashs(IFile *f, IFile *hashoutput, INotEnoughSpaceCallback *cb,
	bool ret_sha2, IFile *copy, bool modify_inplace, int64* inplace_written, IFile* hashinput, bool show_pc)
{
	f->Seek(0);

	hashoutput->Seek(0);
	_i64 fsize=f->Size();
	_i64 fsize_endian = little_endian(fsize);
	if(!writeRepeatFreeSpace(hashoutput, (char*)&fsize_endian, sizeof(_i64), cb))
		return "";

	_i64 input_size;
	if(hashinput!=NULL)
	{
		hashinput->Seek(0);
		if(hashinput->Read(reinterpret_cast<char*>(&input_size), sizeof(input_size))!=sizeof(input_size))
		{
			return "";
		}

		input_size = little_endian(input_size);
	}

	sha_def_ctx ctx;
	if(ret_sha2)
		sha_def_init(&ctx);

	_i64 n_chunks=c_checkpoint_dist/c_small_hash_dist;
	char buf[c_small_hash_dist];
	char copy_buf[c_small_hash_dist];
	_i64 copy_write_pos=0;
	bool copy_read_eof=false;
	char zbuf[big_hash_size]={};
	_i64 hashoutputpos=sizeof(_i64);

	std::auto_ptr<SChunkHashes> chunk_hashes;
	if(hashinput!=NULL)
	{
		chunk_hashes.reset(new SChunkHashes);
	}

	int last_pc=0;
	if(show_pc)
	{
		Server->Log("0%", LL_INFO);
	}

	for(_i64 pos=0;pos<fsize;)
	{
		if(chunk_hashes.get())
		{
			if(pos<input_size)
			{
				hashinput->Seek(hashoutputpos);
				_u32 read = hashinput->Read(chunk_hashes->big_hash, sizeof(SChunkHashes));
				if(read==0)
				{
					chunk_hashes.reset();
				}
			}
			else
			{
				chunk_hashes.reset();
			}
		}

		if(show_pc)
		{
			int curr_pc = (int)( (100.f*pos)/fsize+0.5f );
			if(curr_pc!=last_pc)
			{
				last_pc=curr_pc;
				Server->Log(convert(curr_pc)+"%", LL_INFO);
			}
		}

		_i64 epos=pos+c_checkpoint_dist;
		MD5 big_hash;
		MD5 big_hash_copy_control;
		_i64 hashoutputpos_start=hashoutputpos;
		writeRepeatFreeSpace(hashoutput, zbuf, big_hash_size, cb);
		hashoutputpos+=big_hash_size;
		size_t chunkidx=0;
		_i64 copy_write_pos_start = copy_write_pos;
		for(;pos<epos && pos<fsize;pos+=c_small_hash_dist,++chunkidx)
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
				sha_def_update(&ctx, (unsigned char*)buf, r);
			}
			if(copy!=NULL)
			{
				if(modify_inplace)
				{
					if(chunk_hashes.get())
					{
						if(memcmp(&small_hash, &chunk_hashes->small_hash[chunkidx*small_hash_size], sizeof(small_hash))==0)
						{
							big_hash_copy_control.update((unsigned char*)buf, r);
						}
						else
						{
							//read old data
							copy->Seek(copy_write_pos);
							_u32 copy_r=copy->Read(copy_buf, c_small_hash_dist);

							if(copy_r < c_small_hash_dist)
							{
								copy_read_eof=true;
							}

							big_hash_copy_control.update((unsigned char*)copy_buf, copy_r);

							//write new data
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
						_u32 copy_r;
						if(copy_read_eof)
						{
							copy_r=0;
						}
						else
						{
							copy->Seek(copy_write_pos);
							copy_r=copy->Read(copy_buf, c_small_hash_dist);

							if(copy_r < c_small_hash_dist)
							{
								copy_read_eof=true;
							}
						}

						if(copy_read_eof || copy_r!=r || memcmp(copy_buf, buf, r)!=0)
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

		if(copy!=NULL && chunk_hashes.get() && modify_inplace)
		{
			big_hash_copy_control.finalize();
			if(memcmp(big_hash_copy_control.raw_digest_int(), chunk_hashes->big_hash, big_hash_size)!=0)
			{
				Server->Log("Small hash collision. Copying whole big block...", LL_DEBUG);
				copy_write_pos = copy_write_pos_start;
				pos = epos - c_checkpoint_dist;
				f->Seek(pos);
				for(;pos<epos && pos<fsize;pos+=c_small_hash_dist)
				{
					_u32 r=f->Read(buf, c_small_hash_dist);

					copy->Seek(copy_write_pos);
					if(!writeRepeatFreeSpace(copy, buf, r, cb) )
						return "";

					if(inplace_written!=NULL)
					{
						*inplace_written+=r;
					}

					copy_write_pos+=r;
				}
			}
		}

		hashoutput->Seek(hashoutputpos);
	}

	if(ret_sha2)
	{
		std::string ret;
		ret.resize(64);
		sha_def_final(&ctx, (unsigned char*)&ret[0]);
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
		if(cb!=NULL && cb->handle_not_enough_space(f->getFilename()) )
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
