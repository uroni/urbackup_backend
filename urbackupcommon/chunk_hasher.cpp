/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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
#include "TreeHash.h"
#include <memory.h>
#include <memory>
#include <assert.h>

namespace
{
	std::string build_sparse_extent_content()
	{
		char buf[c_small_hash_dist] = {};
		_u32 small_hash = urb_adler32(urb_adler32(0, NULL, 0), buf, c_small_hash_dist);
		small_hash = little_endian(small_hash);

		MD5 big_hash;
		for (int64 i = 0; i<c_checkpoint_dist; i += c_small_hash_dist)
		{
			big_hash.update(reinterpret_cast<unsigned char*>(buf), c_small_hash_dist);
		}
		big_hash.finalize();

		std::string ret;
		ret.resize(chunkhash_single_size);
		char* ptr = &ret[0];
		memcpy(ptr, big_hash.raw_digest_int(), big_hash_size);
		ptr += big_hash_size;
		for (int64 i = 0; i < c_checkpoint_dist; i += c_small_hash_dist)
		{
			memcpy(ptr, &small_hash, sizeof(small_hash));
			ptr += sizeof(small_hash);
		}
		return ret;
	}

	bool buf_is_zero(const char* buf, size_t bsize)
	{
		for (size_t i = 0; i < bsize; ++i)
		{
			if (buf[i] != 0)
			{
				return false;
			}
		}

		return true;
	}

	std::string sparse_extent_content;
}

std::string get_sparse_extent_content()
{
	assert(!sparse_extent_content.empty());
	return sparse_extent_content;
}

void init_chunk_hasher()
{
	sparse_extent_content = build_sparse_extent_content();
}

bool build_chunk_hashs(IFile *f, IFile *hashoutput, INotEnoughSpaceCallback *cb,
	IFsFile *copy, bool modify_inplace, int64* inplace_written, IFile* hashinput,
	bool show_pc, IHashFunc* hashf, IExtentIterator* extent_iterator,
	std::pair<IFile*, int64> cbt_hash_file)
{
	f->Seek(0);

	hashoutput->Seek(0);
	_i64 fsize=f->Size();
	_i64 fsize_endian = little_endian(fsize);
	if (!writeRepeatFreeSpace(hashoutput, (char*)&fsize_endian, sizeof(_i64), cb))
	{
		Server->Log("Error writing to hashoutput file (" + hashoutput->getFilename() + ")", LL_DEBUG);
		return false;
	}

	_i64 input_size;
	if(hashinput!=NULL)
	{
		hashinput->Seek(0);
		if(hashinput->Read(reinterpret_cast<char*>(&input_size), sizeof(input_size))!=sizeof(input_size))
		{
			Server->Log("Error reading from hashinput file (" + hashinput->getFilename() + ")", LL_DEBUG);
			return false;
		}

		input_size = little_endian(input_size);
	}

	std::vector<char> sha_buf;
	TreeHash* treehash = dynamic_cast<TreeHash*>(hashf);
	if (hashf!=NULL && treehash==NULL)
	{
		sha_buf.resize(c_checkpoint_dist);
	}

	_i64 n_chunks=c_checkpoint_dist/c_small_hash_dist;
	char buf[c_small_hash_dist];
	char copy_buf[c_small_hash_dist];
	_i64 copy_write_pos=0;
	bool copy_read_eof=false;
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

	IFsFile::SSparseExtent curr_extent;

	if (extent_iterator != NULL)
	{
		curr_extent = extent_iterator->nextExtent();
	}

	int64 sparse_extent_start = -1;
	int64 copy_sparse_extent_start = -1;
	int64 copy_max_sparse = -1;
	bool has_sparse_extent = false;

	bool has_more_extents=false;
	std::vector<IFsFile::SFileExtent> extents;
	size_t curr_extent_idx = 0;
	IFsFile* fs_f = dynamic_cast<IFsFile*>(f);
	if (cbt_hash_file.first!=NULL
		&& treehash!=NULL
		&& fs_f!=NULL)
	{
		extents = fs_f->getFileExtents(0, cbt_hash_file.second, has_more_extents);
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

		while (curr_extent.offset != -1
			&& curr_extent.offset + curr_extent.size<pos)
		{
			curr_extent = extent_iterator->nextExtent();
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

		_i64 epos = pos + c_checkpoint_dist;

		if (curr_extent.offset != -1
			&& curr_extent.offset <= pos
			&& curr_extent.offset + curr_extent.size >= epos
			&& epos<=fsize )
		{
			std::string c = get_sparse_extent_content();
			if (!writeRepeatFreeSpace(hashoutput, c.data(), c.size(), cb))
			{
				Server->Log("Error writing to hashoutput file (" + hashoutput->getFilename() + ") -2", LL_DEBUG);
				return false;
			}
			hashoutputpos += c.size();
			

			if (hashf!=NULL && sparse_extent_start == -1)
			{
				sparse_extent_start = pos;
			}

			if (copy_sparse_extent_start == -1)
			{
				copy_sparse_extent_start = pos;

				if (curr_extent.offset < fsize)
				{
					int64 extent_size = curr_extent.size;
					if (curr_extent.offset + extent_size>fsize)
					{
						extent_size = fsize - curr_extent.offset;
					}

					if (copy != NULL && !copy->PunchHole(curr_extent.offset, extent_size))
					{
						std::vector<char> zero_buf;
						zero_buf.resize(32768);

						if (copy->Seek(curr_extent.offset))
						{
							for (int64 written = 0; written < extent_size;)
							{
								_u32 towrite = static_cast<_u32>((std::min)(extent_size - written, static_cast<int64>(zero_buf.size())));
								if (!writeRepeatFreeSpace(copy, zero_buf.data(), towrite, cb))
								{
									Server->Log("Error writing to copy file (" + copy->getFilename() + ")", LL_DEBUG);
									return false;
								}
								written += towrite;
							}
						}
						else
						{
							Server->Log("Error seeking in copy file (" + copy->getFilename() + ")", LL_DEBUG);
							return false;
						}
					}
					else
					{
						copy_max_sparse = (std::min)(fsize, curr_extent.offset + curr_extent.size);
					}
				}
			}

			copy_write_pos += c_checkpoint_dist;

			if (copy != NULL)
			{
				if (!copy->Seek(copy_write_pos))
				{
					Server->Log("Error seeking in copy file (" + copy->getFilename() + ")", LL_DEBUG);
					return false;
				}
			}

			pos = epos;			
			if (!f->Seek(pos))
			{
				Server->Log("Error seeking in input file (" + f->getFilename() + ")", LL_DEBUG);
				return false;
			}
			continue;
		}

		if (!extents.empty()
			&& cbt_hash_file.first != NULL
			&& epos <= fsize
			&& copy==NULL
			&& treehash!=NULL)
		{
			assert(pos%c_checkpoint_dist == 0);

			while (curr_extent_idx < extents.size()
				&& extents[curr_extent_idx].offset + extents[curr_extent_idx].size < pos)
			{
				++curr_extent_idx;

				if (curr_extent_idx >= extents.size()
					&& has_more_extents)
				{
					extents = fs_f->getFileExtents(pos, cbt_hash_file.second, has_more_extents);
					curr_extent_idx = 0;
				}
			}

			if (curr_extent_idx < extents.size()
				&& extents[curr_extent_idx].offset <= pos
				&& extents[curr_extent_idx].offset + extents[curr_extent_idx].size >= epos)
			{
				int64 volume_pos = extents[curr_extent_idx].volume_offset + (pos - extents[curr_extent_idx].offset);
				int64 index_chunkhash_pos = (volume_pos / c_checkpoint_dist)*(sizeof(_u16)+chunkhash_single_size);
				_u16 index_chunkhash_pos_offset = static_cast<_u16>((volume_pos%c_checkpoint_dist) / 512);

				char chunkhash[sizeof(_u16) + chunkhash_single_size];
				if (cbt_hash_file.first->Read(index_chunkhash_pos, chunkhash, sizeof(chunkhash)) == sizeof(chunkhash))
				{
					_u16 chunkhash_offset;
					memcpy(&chunkhash_offset, chunkhash, sizeof(chunkhash_offset));
					if (chunkhash_offset == index_chunkhash_pos_offset
						&& !buf_is_zero(chunkhash, sizeof(chunkhash)))
					{
						if (memcmp(chunkhash+sizeof(_u16), get_sparse_extent_content().data(), chunkhash_single_size) == 0)
						{
							if (sparse_extent_start == -1)
							{
								sparse_extent_start = pos;
							}
						}
						else
						{
							if (sparse_extent_start != -1)
							{
								has_sparse_extent = true;
								int64 end_pos = (pos / c_checkpoint_dist)*c_checkpoint_dist;
								int64 ext_pos[2] = { sparse_extent_start, end_pos - sparse_extent_start };
								hashf->sparse_hash(reinterpret_cast<char*>(ext_pos), sizeof(ext_pos));
								sparse_extent_start = -1;
							}

							treehash->addHashAllAdler(chunkhash + sizeof(_u16), chunkhash_single_size, c_checkpoint_dist);
						}

						if (!writeRepeatFreeSpace(hashoutput, chunkhash + sizeof(_u16), chunkhash_single_size, cb))
						{
							Server->Log("Error writing to hashoutput file (" + hashoutput->getFilename() + ") -3", LL_DEBUG);
							return false;
						}

						hashoutputpos += chunkhash_single_size;

						pos = epos;
						if (!f->Seek(pos))
						{
							Server->Log("Error seeking in input file (" + f->getFilename() + ")", LL_DEBUG);
							return false;
						}
						continue;
					}
				}
			}
		}

		copy_sparse_extent_start = -1;

		
		MD5 big_hash;
		MD5 big_hash_copy_control;
		size_t chunkidx=0;
		_i64 copy_write_pos_start = copy_write_pos;
		SChunkHashes new_chunk;
		_u32 buf_read = 0;
		bool all_zeros = true;
		_i64 start_pos = pos;
		for(;pos<epos && pos<fsize;pos+=c_small_hash_dist,++chunkidx)
		{
			bool has_read_error = false;
			_u32 r=f->Read(buf, c_small_hash_dist, &has_read_error);

			if (has_read_error)
			{
				Server->Log("Error while reading from file \"" + f->getFilename() + "\"", LL_DEBUG);
				return false;
			}

			if (treehash!=NULL && !buf_is_zero(buf, r))
			{
				all_zeros = false;
			}

			*reinterpret_cast<unsigned int*>(&new_chunk.small_hash[chunkidx*small_hash_size]) = urb_adler32(urb_adler32(0, NULL, 0), buf, r);
			big_hash.update((unsigned char*)buf, r);
			buf_read += r;

			if(hashf!=NULL && treehash==NULL)
			{
				int64 buf_offset = pos%sha_buf.size();
				memcpy(sha_buf.data() + buf_offset, buf, r);
			}
			if(copy!=NULL)
			{
				if(modify_inplace)
				{
					if(chunk_hashes.get())
					{
						if(memcmp(&new_chunk.small_hash[chunkidx*small_hash_size], &chunk_hashes->small_hash[chunkidx*small_hash_size], small_hash_size)==0)
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
							if (!writeRepeatFreeSpace(copy, buf, r, cb))
							{
								Server->Log("Error writing to copy file (" + copy->getFilename() + ") -2", LL_DEBUG);
								return false;
							}

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
							if (!writeRepeatFreeSpace(copy, buf, r, cb))
							{
								Server->Log("Error writing to copy file (" + copy->getFilename() + ") -3", LL_DEBUG);
								return false;
							}

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
					if (!writeRepeatFreeSpace(copy, buf, r, cb))
					{
						Server->Log("Error writing to copy file (" + copy->getFilename() + ") -4", LL_DEBUG);
						return false;
					}

					copy_write_pos += r;
				}
			}
		}

		big_hash.finalize();
		memcpy(new_chunk.big_hash, big_hash.raw_digest_int(), 16);

		if (hashf != NULL)
		{
			if (buf_read == c_checkpoint_dist)
			{
				if ( (treehash!=NULL && all_zeros ) 
					|| (treehash==NULL && buf_is_zero(sha_buf.data(), sha_buf.size())) )
				{
					if (sparse_extent_start == -1)
					{
						sparse_extent_start = (start_pos / c_checkpoint_dist)*c_checkpoint_dist;
					}
				}
				else
				{
					if (sparse_extent_start != -1)
					{
						has_sparse_extent = true;
						int64 end_pos = (start_pos / c_checkpoint_dist)*c_checkpoint_dist;
						int64 ext_pos[2] = { sparse_extent_start, end_pos - sparse_extent_start };
						hashf->sparse_hash(reinterpret_cast<char*>(ext_pos), sizeof(ext_pos));
						sparse_extent_start = -1;
					}

					if (treehash != NULL)
					{
						treehash->addHashAllAdler(new_chunk.big_hash, chunkhash_single_size, c_checkpoint_dist);
					}
					else
					{
						hashf->hash(sha_buf.data(), static_cast<unsigned int>(sha_buf.size()));
					}
				}
			}
			else
			{
				if (sparse_extent_start != -1)
				{
					has_sparse_extent = true;
					int64 end_pos = (start_pos / c_checkpoint_dist)*c_checkpoint_dist;

					int64 ext_pos[2] = { sparse_extent_start, end_pos - sparse_extent_start };
					hashf->sparse_hash(reinterpret_cast<char*>(ext_pos), sizeof(ext_pos));

					sparse_extent_start = -1;
				}

				if (treehash != NULL)
				{
					treehash->addHashAllAdler(new_chunk.big_hash, big_hash_size + chunkidx*small_hash_size, buf_read);
				}
				else
				{
					hashf->hash(sha_buf.data(), buf_read);
				}
			}
		}

		for (size_t i = 0; i < chunkidx; ++i)
		{
			new_chunk.small_hash[i] = little_endian(new_chunk.small_hash[i]);
		}

		if (!writeRepeatFreeSpace(hashoutput, new_chunk.big_hash, big_hash_size+chunkidx*small_hash_size, cb))
		{
			Server->Log("Error writing to hashoutput file (" + hashoutput->getFilename() + ") -3", LL_DEBUG);
			return false;
		}

		hashoutputpos += big_hash_size + chunkidx*small_hash_size;

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
					if (!writeRepeatFreeSpace(copy, buf, r, cb))
					{
						Server->Log("Error writing to copy file (" + copy->getFilename() + ") -5", LL_DEBUG);
						return false;
					}

					if(inplace_written!=NULL)
					{
						*inplace_written+=r;
					}

					copy_write_pos+=r;
				}
			}
		}
	}

	if (sparse_extent_start != -1)
	{
		assert(fsize%c_checkpoint_dist ==0);
		has_sparse_extent = true;
		int64 ext_pos[2] = { sparse_extent_start, fsize - sparse_extent_start };
		hashf->sparse_hash(reinterpret_cast<char*>(ext_pos), sizeof(ext_pos));
		sparse_extent_start = -1;
	}

	if (copy != NULL
		&& copy_max_sparse!=-1
		&& copy_max_sparse > copy->Size())
	{
		if (!copy->Resize(copy_max_sparse))
		{
			Server->Log("Error resizing copy file (" + copy->getFilename() + ")", LL_DEBUG);
			return false;
		}
	}

	return true;
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
