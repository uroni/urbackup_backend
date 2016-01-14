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


const int64 sparse_blocksize = 32768;

ChunkPatcher::ChunkPatcher(void)
	: cb(NULL), require_unchanged(true)
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

	const unsigned int buffer_size=32768;
	char buf[buffer_size];

	if(patch->Read((char*)&filesize, sizeof(_i64))!=sizeof(_i64))
	{
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
	bool has_header=true;
	_i64 file_pos;
	_i64 size;
	for(file_pos=0,size=file->Size(); (file_pos<size && file_pos<filesize) || has_header;)
	{
		if(has_header && next_header.patch_off==-1)
		{
			has_header=readNextValidPatch(patch, patchf_pos, &next_header);
		}

		if(!has_header && (file_pos>=filesize || file_pos>=size) )
		{
			break;
		}

		unsigned int tr=buffer_size;
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

			while(next_header.patch_size>0)
			{
				_u32 r=patch->Read((char*)buf, (std::min)((unsigned int)buffer_size, next_header.patch_size));
				patchf_pos+=r;
				if (with_sparse)
				{
					nextChunkPatcherBytes(file_pos, buf, r, true, false);
				}
				else
				{
					cb->next_chunk_patcher_bytes(buf, r, true);
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
					was_sparse = true;
				}
				else
				{
					cb->next_chunk_patcher_bytes(NULL, tr, false);
				}
			}

			while(!was_sparse
				&& tr>0 && file_pos<size && file_pos<filesize)
			{
				if(require_unchanged)
				{
					_u32 r=file->Read((char*)buf, tr);
					
					if (with_sparse)
					{
						nextChunkPatcherBytes(file_pos, buf, r, false, false);
					}
					else
					{
						cb->next_chunk_patcher_bytes(buf, r, false);
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

					file_pos+=tr;
					cb->next_chunk_patcher_bytes(NULL, tr, false);
					tr=0;
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

bool ChunkPatcher::readNextValidPatch(IFile *patchf, _i64 &patchf_pos, SPatchHeader *patch_header)
{
	const unsigned int to_read=sizeof(_i64)+sizeof(unsigned int);
	do
	{
		_u32 r=patchf->Read((char*)&patch_header->patch_off, to_read);
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
	if (sparse)
	{
		if (last_sparse_start == -1)
		{
			last_sparse_start = roundUp(pos, sparse_blocksize);
		}
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

		for (size_t i = 0; i < bsize_to_checkpoint; ++i)
		{
			if (buf[i] != 0)
			{
				curr_only_zeros = false;
				break;
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
	if (sparse_buf_used > 0)
	{
		cb->next_chunk_patcher_bytes(sparse_buf.data(), sparse_buf_used, curr_changed);
	}
}

void ChunkPatcher::finishSparse(int64 pos)
{
	if (pos > last_sparse_start)
	{
		IFsFile::SSparseExtent ext(last_sparse_start, pos - last_sparse_start);
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

void ChunkPatcher::setWithSparse(bool b)
{
	with_sparse = b;
}
