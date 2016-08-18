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

#include "ChunkSendThread.h"
#include "CClientThread.h"
#include "packet_ids.h"
#include "log.h"
#include "../stringtools.h"
#include "socket_header.h"
#include <memory.h>
#include <assert.h>

#include "../Interface/File.h"
#include "../Interface/Server.h"
#include "../common/adler32.h"

#ifndef _WIN32
#include <errno.h>
#endif
#include "PipeSessions.h"

namespace
{
	unsigned int getSystemErrorCode()
	{
#ifdef _WIN32
		return GetLastError();
#else
		return errno;
#endif
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
}


ChunkSendThread::ChunkSendThread(CClientThread *parent)
	: parent(parent), file(NULL), has_error(false), cbt_hash_file_info()
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
	while(parent->getNextChunk(&chunk, has_error))
	{
		if(chunk.msg != ID_ILLEGAL && chunk.msg!= ID_FREE_SERVER_FILE && chunk.msg!= ID_FLUSH_SOCKET)
		{
			if(parent->SendInt(reinterpret_cast<char*>(&chunk.msg), 1, true)==SOCKET_ERROR)
			{
				has_error = true;
			}
		}
		else if (chunk.msg == ID_FREE_SERVER_FILE)
		{
			if (pipe_file_user.get()==NULL && file != NULL)
			{
				Server->Log("Closing file (free) " + file->getFilename(), LL_DEBUG);
				Server->destroy(file);
				assert(!s_filename.empty());
				FileServ::decrShareActive(s_filename);
				file = NULL;
			}
			else if (pipe_file_user.get() != NULL)
			{
				file = NULL;
			}
			pipe_file_user.reset();

			if (cbt_hash_file_info.cbt_hash_file != NULL
				&& cbt_hash_file_info.metadata_offset != -1)
			{
				Server->destroy(cbt_hash_file_info.cbt_hash_file);
				cbt_hash_file_info.cbt_hash_file = NULL;
			}
		}
		else if (chunk.msg == ID_FLUSH_SOCKET)
		{
			Server->Log("Received flush.", LL_DEBUG);
			if (!parent->FlushInt())
			{
				Server->Log("Error flushing output socket", LL_INFO);
			}
		}
		else if(chunk.update_file!=NULL)
		{
			if(pipe_file_user.get() == NULL && file!=NULL)
			{
				Server->Log("Closing file " + file->getFilename(), LL_DEBUG);
				Server->destroy(file);
				assert(!s_filename.empty());
				FileServ::decrShareActive(s_filename);
			}
			if (cbt_hash_file_info.cbt_hash_file != NULL
				&& cbt_hash_file_info.metadata_offset != -1)
			{
				Server->destroy(cbt_hash_file_info.cbt_hash_file);
				cbt_hash_file_info.cbt_hash_file = NULL;
			}
			file=chunk.update_file;
			Server->Log("Retaining file " + file->getFilename(), LL_DEBUG);
			s_filename = chunk.s_filename;
			curr_hash_size=chunk.hashsize;
			curr_file_size=chunk.startpos;
			cbt_hash_file_info = chunk.cbt_hash_file_info;
			pipe_file_user.reset(chunk.pipe_file_user);
			file_extents.clear();
			has_more_extents = true;

			std::vector<IFsFile::SSparseExtent> sparse_extents;
			if (chunk.with_sparse)
			{
				IFsFile* fs_file = reinterpret_cast<IFsFile*>(file);
				IFsFile::SSparseExtent new_extent;
				do
				{
					new_extent = fs_file->nextSparseExtent();
					if (new_extent.offset != -1)
					{
						sparse_extents.push_back(new_extent);
					}

				} while (new_extent.offset != -1);
			}

			CWData sdata;
			if (!sparse_extents.empty())
			{
				sdata.addUChar(ID_FILESIZE_AND_EXTENTS);
			}
			else
			{
				sdata.addUChar(ID_FILESIZE);
			}
			sdata.addUInt64(curr_file_size);

			if (!sparse_extents.empty())
			{
				sdata.addUInt64(sparse_extents.size());
			}

            if(parent->SendInt(sdata.getDataPtr(), sdata.getDataSize(), chunk.requested_filesize<0)!=sdata.getDataSize())
			{
				has_error = true;
			}
			else if (!sparse_extents.empty())
			{
				MD5 sparse_hash;
				sparse_hash.update(reinterpret_cast<unsigned char*>(sparse_extents.data()), static_cast<_u32>(sparse_extents.size()*sizeof(IFsFile::SSparseExtent)));
				sparse_hash.finalize();

				IFsFile::SSparseExtent hash_extent;
				memcpy(&hash_extent, sparse_hash.raw_digest_int(), sizeof(hash_extent));

				sparse_extents.push_back(hash_extent);

				if (parent->SendInt(reinterpret_cast<char*>(sparse_extents.data()), sparse_extents.size()*sizeof(IFsFile::SSparseExtent)) != sparse_extents.size()*sizeof(IFsFile::SSparseExtent))
				{
					has_error = true;
				}
			}
		}
		else
		{
			if( FileServ::isPause() )
			{
				Sleep(500);
			}
			if(!sendChunk(&chunk))
			{
				has_error = true;
			}
			else if (file->Size() == -1)
			{
				if (!parent->FlushInt())
				{
					Server->Log("Error flushing output socket", LL_INFO);
				}
			}
		}
	}

	if(pipe_file_user.get() == NULL && file!=NULL)
	{
		Server->Log("Closing file (finish) " + file->getFilename(), LL_DEBUG);
		Server->destroy(file);
		assert(!s_filename.empty());
		FileServ::decrShareActive(s_filename);
		file=NULL;
	}

	if (cbt_hash_file_info.cbt_hash_file != NULL
		&& cbt_hash_file_info.metadata_offset != -1)
	{
		Server->destroy(cbt_hash_file_info.cbt_hash_file);
		cbt_hash_file_info.cbt_hash_file = NULL;
	}

	delete this;
}

bool ChunkSendThread::sendChunk(SChunk *chunk)
{
	if(file==NULL)
	{
		return false;
	}

	int64 spos = chunk->startpos;
	if(!file->Seek(spos))
	{
		return sendError(ERR_SEEKING_FAILED, getSystemErrorCode());
	}

	if(chunk->transfer_all)
	{
		size_t off=1+sizeof(_i64)+sizeof(_u32);
		*chunk_buf=ID_WHOLE_BLOCK;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));

		unsigned int blockleft;
		if(curr_file_size<=chunk->startpos && curr_file_size>0)
		{
			blockleft=0;
		}
		else if(curr_file_size-chunk->startpos<c_checkpoint_dist && curr_file_size>0)
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

		Log("Sending whole block start="+convert(chunk->startpos)+" size="+convert(blockleft), LL_DEBUG);
		_u32 r;

		bool script_eof=false;
		bool off_sent=false;
		bool readerr = false;
		unsigned int readderr_code = 0;

		do
		{
			r=0;
			if(blockleft>0)
			{
				_u32 toread = (std::min)(blockleft, c_chunk_size);

				while(r<toread)
				{
					bool c_readerr = false;
					_u32 r_add=file->Read(spos, chunk_buf+off+r,  toread-r, &c_readerr);

					if (c_readerr)
					{
						readerr = true;
						readderr_code = getSystemErrorCode();
						Server->Log("Reading from file \"" + file->getFilename() + "\" at position "+convert(spos)+" failed (code: " + convert(readderr_code) + ")(1).", LL_ERROR);
						FileServ::callErrorCallback(s_filename, file->getFilename(), spos, "code: " + convert(readderr_code));
						memset(chunk_buf + off + r + r_add, 0, (toread - r) - r_add);
						r_add = toread - r;
					}

					spos += r_add;

					if(r_add==0 && file->Size()!=-1)
					{
						if(curr_file_size==-1)
						{
							if(!script_eof)
							{
								Log("Script output eof", LL_DEBUG);
								script_eof=true;
							}

							memset(chunk_buf+off+r, 0, toread-r);
							r=toread;
						}
						else
						{
							memset(chunk_buf+off+r, 0, toread-r);
							r=toread;
						}
					}
					else if(r_add>0)
					{
						md5_hash.update((unsigned char*)chunk_buf+off+r, r_add);

						if(parent->SendInt(chunk_buf+(off_sent?off:0)+r, (off_sent?0:off)+r_add, r_add<toread)==SOCKET_ERROR)
						{
							Log("Error sending whole block", LL_DEBUG);
							return false;
						}

						if( FileServ::isPause() ) Sleep(500);

						off_sent=true;
					}
					r+=r_add;
				}				
			}

			if(r<=blockleft)
				blockleft-=r;
			else
				blockleft=0;

		}
		while(r==c_chunk_size && blockleft>0);	

		if (readerr)
		{
			return sendError(ERR_READING_FAILED, readderr_code);
		}

		md5_hash.finalize();
		*chunk_buf=ID_BLOCK_HASH;
		chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);

		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
		{
			Log("Error sending block hash", LL_DEBUG);
			return false;
		}

		if( FileServ::isPause() ) Sleep(500);

		if(script_eof)
		{
			parent->SendInt(NULL, 0);
			return false;
		}

		return true;
	}
	unsigned int next_smallhash=c_small_hash_dist;

	unsigned int read_total=0;
	_u32 r=0;
	_u32 real_r=0;
	bool sent_update=false;
	char* cptr=chunk_buf+c_chunk_padding;
	_i64 curr_pos=chunk->startpos;
	unsigned int c_adler=urb_adler32(0, NULL, 0);
	md5_hash.init();
	unsigned int small_hash_num=0;
	bool script_eof=false;
	bool cbt_unchanged = false;
	int64 index_chunkhash_pos = -1;

	if (cbt_hash_file_info.cbt_hash_file!=NULL
		&&  curr_pos+c_checkpoint_dist<=curr_file_size
		&& (cbt_hash_file_info.metadata_offset!=-1
			|| !file_extents.empty()
			|| has_more_extents ) )
	{
		if (cbt_hash_file_info.metadata_offset == -1)
		{
			if (file_extents.empty())
			{
				IFsFile* fs_file = reinterpret_cast<IFsFile*>(file);
				if (fs_file != NULL
					&& cbt_hash_file_info.blocksize > 0)
				{
					file_extents = fs_file->getFileExtents(spos, cbt_hash_file_info.blocksize, has_more_extents);
				}
			}

			for (size_t i = 0; i < file_extents.size(); ++i)
			{
				if (file_extents[i].offset <= spos &&
					file_extents[i].offset + file_extents[i].size >= spos + c_checkpoint_dist)
				{
					int64 volume_pos = file_extents[i].volume_offset + (spos - file_extents[i].offset);
					index_chunkhash_pos = (volume_pos / c_checkpoint_dist)*chunkhash_single_size;

					char chunkhash[chunkhash_single_size];
					if (cbt_hash_file_info.cbt_hash_file->Read(index_chunkhash_pos, chunkhash, chunkhash_single_size) == chunkhash_single_size)
					{
						if (memcmp(chunkhash, chunk->big_hash, chunkhash_single_size) == 0)
						{
							cbt_unchanged = true;
							index_chunkhash_pos = -1;
						}
						else if (!buf_is_zero(chunkhash, chunkhash_single_size))
						{
							index_chunkhash_pos = -1;
						}
					}
					else
					{
						index_chunkhash_pos = -1;
					}

					break;
				}
			}
		}
		else
		{
			char chunkhash[chunkhash_single_size];
			int64 hash_rpos = cbt_hash_file_info.metadata_offset + (spos / c_checkpoint_dist)*chunkhash_single_size;
			if ( hash_rpos + chunkhash_single_size <= cbt_hash_file_info.metadata_offset+cbt_hash_file_info.metadata_size
				&& cbt_hash_file_info.cbt_hash_file->Read(hash_rpos, chunkhash, chunkhash_single_size) == chunkhash_single_size)
			{
				if (memcmp(chunkhash, chunk->big_hash, chunkhash_single_size) == 0)
				{
					cbt_unchanged = true;
				}
			}
		}
	}

	std::vector<char> new_chunkhashes;
	if (index_chunkhash_pos != -1)
	{
		new_chunkhashes.resize(chunkhash_single_size);
	}

	if (!cbt_unchanged)
	{
		do
		{
			cptr += r;

			_u32 to_read = c_chunk_size;

			if (curr_file_size <= curr_pos && curr_file_size > 0)
			{
				to_read = 0;
			}
			else if (curr_file_size - curr_pos < to_read && curr_file_size>0)
			{
				to_read = static_cast<_u32>(curr_file_size - curr_pos);
			}

			bool readerr = false;

			r = file->Read(spos, cptr, to_read, &readerr);
			spos += r;
			real_r = r;

			if (readerr)
			{
				unsigned int readderr_code = getSystemErrorCode();
				Server->Log("Reading from file \"" + file->getFilename() + "\" at position " + convert(spos) + " failed (code: " + convert(readderr_code) + ")(2).", LL_ERROR);
				FileServ::callErrorCallback(s_filename, file->getFilename(), spos, "code: " + convert(readderr_code));
				return sendError(ERR_READING_FAILED, readderr_code);
			}

			while (r < to_read)
			{
				_u32 r_add = file->Read(spos, cptr + r, to_read - r, &readerr);
				spos += r_add;

				if (readerr)
				{
					unsigned int readderr_code = getSystemErrorCode();
					Server->Log("Reading from file \"" + file->getFilename() + "\" at position " + convert(spos) + " failed (code: " + convert(readderr_code) + ")(3).", LL_ERROR);
					FileServ::callErrorCallback(s_filename, file->getFilename(), spos, "code: " + convert(readderr_code));
					return sendError(ERR_READING_FAILED, readderr_code);
				}

				if (r_add == 0 && file->Size() != -1)
				{
					if (curr_file_size == -1)
					{
						if (!script_eof)
						{
							Log("Script output eof -2", LL_DEBUG);
							script_eof = true;
						}

						break;
					}
					else
					{
						memset(cptr + r, 0, to_read - r);
						r = to_read;
					}
				}

				r += r_add;
				real_r += r_add;
			}

			md5_hash.update((unsigned char*)cptr, (unsigned int)r);
			c_adler = urb_adler32(c_adler, cptr, r);

			read_total += r;

			if (read_total == next_smallhash || r != c_chunk_size)
			{
				_u32 adler_other = little_endian(*((_u32*)&chunk->small_hash[small_hash_size*small_hash_num]));
				if (c_adler != adler_other
					|| curr_pos + r > curr_hash_size)
				{
					sent_update = true;
					char tmp_backup[c_chunk_padding];
					memcpy(tmp_backup, cptr - c_chunk_padding, c_chunk_padding);

					*(cptr - c_chunk_padding) = ID_UPDATE_CHUNK;
					_i64 curr_pos_tmp = little_endian(curr_pos);
					memcpy(cptr - sizeof(_i64) - sizeof(_u32), &curr_pos_tmp, sizeof(_i64));
					_u32 r_tmp = little_endian(r);
					memcpy(cptr - sizeof(_u32), &r_tmp, sizeof(_u32));

					Log("Sending chunk start=" + convert(curr_pos) + " size=" + convert(r), LL_DEBUG);

					if (parent->SendInt(cptr - c_chunk_padding, c_chunk_padding + r) == SOCKET_ERROR)
					{
						Log("Error sending chunk", LL_DEBUG);
						return false;
					}

					if (FileServ::isPause()) Sleep(500);

					memcpy(cptr - c_chunk_padding, tmp_backup, c_chunk_padding);
				}

				if (!new_chunkhashes.empty())
				{
					_u32 little_c_addler = little_endian(c_adler);
					memcpy(&new_chunkhashes[big_hash_size + small_hash_size*small_hash_num], &little_c_addler, sizeof(little_c_addler));
				}

				c_adler = urb_adler32(0, NULL, 0);
				++small_hash_num;
				next_smallhash += c_small_hash_dist;
			}
			curr_pos += r;

		} while (real_r == c_chunk_size && read_total < c_checkpoint_dist);
	}

	md5_hash.finalize();

	if (!new_chunkhashes.empty()
		&& *cbt_hash_file_info.snapshot_sequence_id == cbt_hash_file_info.snapshot_sequence_id_reference)
	{
		memcpy(new_chunkhashes.data(), md5_hash.raw_digest_int(), big_hash_size);
		cbt_hash_file_info.cbt_hash_file->Write(index_chunkhash_pos, new_chunkhashes.data(), chunkhash_single_size);
	}

	if(!sent_update && !cbt_unchanged && memcmp(md5_hash.raw_digest_int(), chunk->big_hash, big_hash_size)!=0 )
	{
		Log("Sending whole block(2) start="+convert(chunk->startpos)+" size="+convert(read_total), LL_DEBUG);

		*chunk_buf=ID_WHOLE_BLOCK;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		unsigned int read_total_tmp = little_endian(read_total);
		memcpy(chunk_buf+1+sizeof(_i64), &read_total_tmp, sizeof(_u32));
		if(parent->SendInt(chunk_buf, read_total+1+sizeof(_i64)+sizeof(_u32))==SOCKET_ERROR)
		{
			Log("Error sending whole block", LL_DEBUG);
			return false;
		}

		if( FileServ::isPause() ) Sleep(500);

		*chunk_buf=ID_BLOCK_HASH;
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
		{
			Log("Error sending whole block hash", LL_DEBUG);
			return false;
		}

		if( FileServ::isPause() ) Sleep(500);
	}
	else if(!sent_update)
	{
		*chunk_buf=ID_NO_CHANGE;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64))==SOCKET_ERROR)
		{
			Log("Error sending no change", LL_DEBUG);
			return false;
		}

		if( FileServ::isPause() ) Sleep(500);
	}
	else
	{
		*chunk_buf=ID_BLOCK_HASH;
		_i64 chunk_startpos = little_endian(chunk->startpos);
		memcpy(chunk_buf+1, &chunk_startpos, sizeof(_i64));
		memcpy(chunk_buf+1+sizeof(_i64), md5_hash.raw_digest_int(), big_hash_size);
		if(parent->SendInt(chunk_buf, 1+sizeof(_i64)+big_hash_size)==SOCKET_ERROR)
		{
			Log("Error sending block hash");
			return false;
		}

		if( FileServ::isPause() ) Sleep(500);
	}

	if(script_eof)
	{
		parent->SendInt(NULL, 0);
		return false;
	}

	return true;
}

bool ChunkSendThread::sendError( _u32 errorcode1, _u32 errorcode2 )
{
	char buffer[1+sizeof(_u32)*2];
	*buffer = ID_BLOCK_ERROR;
	memcpy(buffer+1, &errorcode1, sizeof(_u32));
	memcpy(buffer+1+sizeof(_u32), &errorcode2, sizeof(_u32));

	if(parent->SendInt(buffer, 1+sizeof(_u32)*2, true)==SOCKET_ERROR)
	{
		Log("Error sending error paket");
		return false;
	}
	else
	{
		return true;
	}
}
