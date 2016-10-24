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

#include "ChunkPatcher.h"
#include "../stringtools.h"
#include <assert.h>
#include "../urbackupcommon/ExtentIterator.h"
#include <memory.h>
#include <limits.h>

#define VLOG(x)


namespace
{
	int64 roundUp(int64 numToRound, int64 multiple)
	{
		return ((numToRound + multiple - 1) / multiple) * multiple;
	}

	int64 roundDown(int64 numToRound, int64 multiple)
	{
		return ((numToRound / multiple) * multiple);
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


const int64 sparse_blocksize = 512*1024;

ChunkPatcher::ChunkPatcher(void)
	: cb(NULL), require_unchanged(true), with_sparse(false),
	unchanged_align(0), unchanged_align_start(-1), unchanged_align_end(-1), unchanged_align_end_next(-1), last_unchanged(false)
{
}

void ChunkPatcher::setCallback(IChunkPatcherCallback *pCb)
{
	cb=pCb;
}

bool ChunkPatcher::ApplyPatch(IFile *file, IFile *patch, ExtentIterator* extent_iterator)
{
	patch->Seek(0);
	file->Seek(0);
	_i64 patchf_pos=0;
	unchanged_align_start = -1;
	unchanged_align_end = -1;
	unchanged_align_end_next = -1;
	last_unchanged = false;

	const unsigned int buffer_size=512*1024;
	std::vector<char> buf;
	buf.resize(buffer_size);

	if(patch->Read((char*)&filesize, sizeof(_i64))!=sizeof(_i64))
	{
		Server->Log("Error reading patched file size from \""+patch->getFilename()+"\"", LL_ERROR);
		return false;
	}

	filesize = little_endian(filesize);

	patchf_pos+=sizeof(_i64);

	if (with_sparse || extent_iterator !=NULL)
	{
		last_sparse_start = -1;
		curr_only_zeros = true;
		curr_changed = false;
		sparse_buf.resize(sparse_blocksize);
	}

	IFsFile::SSparseExtent curr_sparse_extent;
	if (extent_iterator != NULL)
	{
		curr_sparse_extent = extent_iterator->nextExtent();
	}

	SPatchHeader next_header;
	next_header.patch_off=-1;
	next_header.patch_size = 0;
	bool has_header=true;
	_i64 file_pos;
	_i64 size;
	for(file_pos=0,size=file->Size(); (file_pos<size && file_pos<filesize) || has_header;)
	{
		if(has_header && next_header.patch_off==-1)
		{
			bool has_read_error = false;
			has_header=readNextValidPatch(patch, patchf_pos, &next_header, has_read_error);
			if (has_read_error)
			{
				Server->Log("Read error while reading next patch from \""+patch->getFilename()+"\"", LL_ERROR);
				return false;
			}

			if (next_header.patch_off != -1
				&& unchanged_align != 0)
			{
				unchanged_align_start = roundDown(next_header.patch_off, unchanged_align);
				unchanged_align_end_next = roundUp(next_header.patch_off + next_header.patch_size, unchanged_align);
			}
		}

		if(!has_header && (file_pos>=filesize || file_pos>=size) )
		{
			break;
		}

		unsigned int tr = UINT_MAX;
		if(next_header.patch_off!=-1)
		{
			_i64 hoff=next_header.patch_off-file_pos;
			if(hoff>=0 && hoff<tr)
				tr=(unsigned int)hoff;

			assert(hoff>=0);
		}

		bool patching_finished=false;
		if(tr==0 && file_pos+next_header.patch_size>filesize)
		{
			next_header.patch_size = static_cast<unsigned int>(filesize - file_pos);
			patching_finished=true;
		}
		else if(file_pos>=filesize)
		{
			Server->Log("Patch corrupt file_pos>=filesize. file_pos="+convert(file_pos)+" next_header.patch_off="+convert(next_header.patch_off)+" next_header.patch_size="+convert(next_header.patch_size)+" tr="+convert(tr)+" size="+convert(size)+" filesize="+convert(filesize)+" has_header="+convert(has_header), LL_ERROR);
			assert(file_pos<filesize);
			return false;
		}

		if(tr==0)
		{
			assert(file_pos==next_header.patch_off);

			VLOG(Server->Log("Applying patch at "+convert(file_pos)+" length="+convert(next_header.patch_size), LL_DEBUG));

			unchanged_align_end = unchanged_align_end_next;

			while(next_header.patch_size>0)
			{
				bool has_read_error = false;
				_u32 r=patch->Read(buf.data(), (std::min)((unsigned int)buffer_size, next_header.patch_size), &has_read_error);

				if (has_read_error)
				{
					Server->Log("Read error while reading patch data from \""+patch->getFilename()+"\"", LL_ERROR);
					return false;
				}

				patchf_pos+=r;
				if (with_sparse)
				{
					nextChunkPatcherBytes(file_pos, buf.data(), r, true, false);
				}
				else
				{
					cb->next_chunk_patcher_bytes(buf.data(), r, true);
				}				
				next_header.patch_size-=r;
				file_pos += r;
			}
			if(require_unchanged)
			{
				file->Seek(file_pos);
			}
			next_header.patch_off=-1;
		}
		else if(file_pos<size && file_pos<filesize)
		{
			while (curr_sparse_extent.offset != -1
				&& curr_sparse_extent.offset+curr_sparse_extent.size <= file_pos)
			{
				curr_sparse_extent = extent_iterator->nextExtent();
			}

			if (file_pos + tr>filesize)
			{
				tr = static_cast<unsigned int>(filesize - file_pos);
			}

			bool was_sparse = false;
			if (curr_sparse_extent.offset != -1
				&& curr_sparse_extent.offset <= file_pos
				&& curr_sparse_extent.offset + curr_sparse_extent.size >= file_pos + tr)
			{
				if (with_sparse)
				{
					nextChunkPatcherBytes(file_pos, NULL, tr, false, true);
					file_pos += tr;
				}
				else
				{
					bool is_sparse = true;
					cb->next_chunk_patcher_bytes(NULL, tr, false, &is_sparse);
					file_pos += tr;
				}
				was_sparse = true;
			}

			while(!was_sparse
				&& tr>0 && file_pos<size && file_pos<filesize)
			{
				tr = (std::min)(tr, buffer_size);

				bool curr_require_unchaged = require_unchanged;

				if (unchanged_align != 0)
				{
					if ( (unchanged_align_start != -1
							&& unchanged_align_end_next != -1
							&& file_pos >= unchanged_align_start
							&& file_pos < unchanged_align_end_next ) 
						|| (unchanged_align_end != -1
							&& file_pos < unchanged_align_end ) )
					{
						curr_require_unchaged = true;
						file->Seek(file_pos);
					}

					if (!curr_require_unchaged 
						&& unchanged_align_start != -1
						&& file_pos < unchanged_align_start
						&& file_pos + tr > unchanged_align_start)
					{
						tr = static_cast<_u32>(unchanged_align_start - file_pos);
					}
					else if (unchanged_align_end!= -1
						&& file_pos < unchanged_align_end
						&& file_pos + tr > unchanged_align_end)
					{
						tr = static_cast<_u32>(unchanged_align_end - file_pos);
					}
				}

				if(curr_require_unchaged)
				{
					bool has_read_error = false;
					_u32 r=file->Read(buf.data(), tr, &has_read_error);

					if (has_read_error)
					{
						Server->Log("Read error while reading unchanged data from \""+file->getFilename()+"\"", LL_ERROR);
						return false;
					}
					
					if (with_sparse)
					{
						nextChunkPatcherBytes(file_pos, buf.data(), r, false, false);
					}
					else
					{
						cb->next_chunk_patcher_bytes(buf.data(), r, false);
					}
					
					file_pos += r;
					tr-=r;
				}
				else
				{
					if(file_pos+tr>size)
					{
						tr=static_cast<unsigned int>(size-file_pos);
					}

					bool is_sparse = false;
					bool* p_is_sparse = &is_sparse;

					if (unchanged_align != 0
						&& tr < unchanged_align
						&& last_sparse_start == -1)
					{
						p_is_sparse = NULL;
					}

					cb->next_chunk_patcher_bytes(NULL, tr, false, p_is_sparse);
					last_unchanged = true;

					if (is_sparse)
					{
						if (last_sparse_start == -1)
						{
							last_sparse_start = file_pos;
						}
					}
					else if (last_sparse_start != -1)
					{
						finishSparse(file_pos);
					}

					file_pos += tr;
					
					tr=0;
				}

				if (unchanged_align != 0)
				{
					if (file_pos == unchanged_align_end)
					{
						unchanged_align_end = -1;
					}
				}
			}
		}
		else
		{
			Server->Log("Patch corrupt. file_pos="+convert(file_pos)+" next_header.patch_off="+convert(next_header.patch_off)+" next_header.patch_size="+convert(next_header.patch_size)+" tr="+convert(tr)+" size="+convert(size)+" filesize="+convert(filesize), LL_ERROR);
			assert(false);
			return false;
		}

		if(patching_finished)
		{
			if (with_sparse)
			{
				finishChunkPatcher(file_pos);
			}
			return true;
		}
	}
	if (with_sparse)
	{
		finishChunkPatcher(file_pos);
	}
	return true;
}

bool ChunkPatcher::readNextValidPatch(IFile *patchf, _i64 &patchf_pos, SPatchHeader *patch_header, bool& has_read_error)
{
	const unsigned int to_read=sizeof(_i64)+sizeof(unsigned int);
	do
	{
		_u32 r=patchf->Read((char*)&patch_header->patch_off, to_read, &has_read_error);
		patchf_pos+=r;
		if(r!=to_read)
		{
			patch_header->patch_off=-1;
			patch_header->patch_size=0;
			return false;
		}
		else
		{
			patch_header->patch_off = little_endian(patch_header->patch_off);
			patch_header->patch_size = little_endian(patch_header->patch_size);
		}

		if(patch_header->patch_off==-1)
		{
			patchf_pos+=patch_header->patch_size;
			patchf->Seek(patchf_pos);
		}
	}
	while(patch_header->patch_off==-1);

	return true;
}

void ChunkPatcher::nextChunkPatcherBytes(int64 pos, const char * buf, size_t bsize, bool changed, bool sparse)
{
	last_unchanged = false;

	if (sparse)
	{
		if (last_sparse_start == -1)
		{
			last_sparse_start = roundUp(pos, sparse_blocksize);
		}

		bool is_sparse = true;
		cb->next_chunk_patcher_bytes(NULL, bsize, changed, &is_sparse);

		return;
	}

	while (bsize > 0)
	{
		if (pos%sparse_blocksize == 0 && bsize == sparse_blocksize)
		{
			curr_only_zeros = buf_is_zero(buf, bsize);

			if (curr_only_zeros)
			{
				if (last_sparse_start == -1)
				{
					last_sparse_start = pos;
				}

				bool is_sparse = true;
				cb->next_chunk_patcher_bytes(NULL, bsize, changed, &is_sparse);

				return;
			}
			else if (last_sparse_start != -1)
			{
				finishSparse(pos);
			}

			cb->next_chunk_patcher_bytes(buf, bsize, changed);

			curr_only_zeros = true;
			curr_changed = false;

			return;
		}

		int64 next_checkpoint = roundDown(pos, sparse_blocksize) + sparse_blocksize;

		size_t bsize_to_checkpoint = (std::min)(bsize, static_cast<size_t>(next_checkpoint - pos));
		int64 sparse_buf_used = pos%sparse_blocksize;

		if (curr_only_zeros)
		{
			for (size_t i = 0; i < bsize_to_checkpoint; ++i)
			{
				if (buf[i] != 0)
				{
					curr_only_zeros = false;
					break;
				}
			}
		}

		if (changed)
		{
			curr_changed = true;
		}

		memcpy(sparse_buf.data() + sparse_buf_used, buf, bsize_to_checkpoint);

		if (pos + bsize_to_checkpoint == next_checkpoint)
		{
			if (curr_only_zeros)
			{
				if (last_sparse_start == -1)
				{
					last_sparse_start = next_checkpoint-sparse_blocksize;
				}
			}
			else if(last_sparse_start != -1)
			{
				finishSparse(pos);
			}

			if (!curr_only_zeros)
			{
				cb->next_chunk_patcher_bytes(sparse_buf.data(), sparse_blocksize, curr_changed);
			}
			else
			{
				bool is_sparse = true;
				cb->next_chunk_patcher_bytes(NULL, sparse_blocksize, curr_changed, &is_sparse);
			}

			curr_only_zeros = true;
			curr_changed = false;
		}

		pos += bsize_to_checkpoint;
		buf += bsize_to_checkpoint;
		bsize -= bsize_to_checkpoint;
	}
}

void ChunkPatcher::finishChunkPatcher(int64 pos)
{
	finishSparse(pos);

	int64 sparse_buf_used = pos%sparse_blocksize;
	if (sparse_buf_used > 0 && !last_unchanged)
	{
		cb->next_chunk_patcher_bytes(sparse_buf.data(), static_cast<size_t>(sparse_buf_used), curr_changed);
	}
}

void ChunkPatcher::finishSparse(int64 pos)
{
	if (last_sparse_start!=-1 && roundDown(pos, sparse_blocksize) > last_sparse_start)
	{
		IFsFile::SSparseExtent ext(last_sparse_start, roundDown(pos, sparse_blocksize) - last_sparse_start);
		cb->next_sparse_extent_bytes(reinterpret_cast<char*>(&ext), sizeof(IFsFile::SSparseExtent));
		last_sparse_start = -1;
	}
}

_i64 ChunkPatcher::getFilesize(void)
{
	return filesize;
}

void ChunkPatcher::setRequireUnchanged( bool b )
{
	require_unchanged=b;
}

void ChunkPatcher::setUnchangedAlign(int64 a)
{
	unchanged_align = a;
}

void ChunkPatcher::setWithSparse(bool b)
{
	with_sparse = b;
}
