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

#include "ChunkPatcher.h"
#include "../stringtools.h"
#include <assert.h>

#define VLOG(x)

ChunkPatcher::ChunkPatcher(void)
	: cb(NULL), require_unchanged(true)
{
}

void ChunkPatcher::setCallback(IChunkPatcherCallback *pCb)
{
	cb=pCb;
}

bool ChunkPatcher::ApplyPatch(IFile *file, IFile *patch)
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

	SPatchHeader next_header;
	next_header.patch_off=-1;
	bool has_header=true;
	for(_i64 file_pos=0,size=file->Size(); (file_pos<size && file_pos<filesize) || has_header;)
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

			file_pos+=next_header.patch_size;

			while(next_header.patch_size>0)
			{
				_u32 r=patch->Read((char*)buf, (std::min)((unsigned int)buffer_size, next_header.patch_size));
				patchf_pos+=r;
				cb->next_chunk_patcher_bytes(buf, r, true);
				next_header.patch_size-=r;
			}
			if(require_unchanged)
			{
				file->Seek(file_pos);
			}
			next_header.patch_off=-1;
		}
		else if(file_pos<size && file_pos<filesize)
		{
			while(tr>0 && file_pos<size && file_pos<filesize)
			{
				if(file_pos+tr>filesize)
				{
					tr=static_cast<unsigned int>(filesize-file_pos);
				}

				if(require_unchanged)
				{
					_u32 r=file->Read((char*)buf, tr);
					file_pos+=r;
					cb->next_chunk_patcher_bytes(buf, r, false);
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
			return true;
		}
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

_i64 ChunkPatcher::getFilesize(void)
{
	return filesize;
}

void ChunkPatcher::setRequireUnchanged( bool b )
{
	require_unchanged=b;
}
