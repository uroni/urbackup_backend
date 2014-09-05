#include "ChunkPatcher.h"
#include "../stringtools.h"

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

		unsigned int tr=buffer_size;
		if(next_header.patch_off!=-1)
		{
			_i64 hoff=next_header.patch_off-file_pos;
			if(hoff>=0 && hoff<tr)
				tr=(unsigned int)hoff;
		}

		if(file_pos>=size || file_pos>=filesize || tr==0)
		{
			file_pos+=next_header.patch_size;

			while(next_header.patch_size>0)
			{
				_u32 r=patch->Read((char*)buf, (std::min)((unsigned int)buffer_size, next_header.patch_size));
				patchf_pos+=r;
				cb->next_chunk_patcher_bytes(buf, r, true);
				next_header.patch_size-=r;
			}
			file->Seek(file_pos);
			next_header.patch_off=-1;
		}
		else if(file_pos<size && file_pos<filesize)
		{
			while(tr>0 && file_pos<size && file_pos<filesize)
			{
				if(require_unchanged)
				{
					if(file_pos+tr>filesize)
					{
						tr=static_cast<unsigned int>(filesize-file_pos);
					}
					_u32 r=file->Read((char*)buf, tr);
					file_pos+=r;
					cb->next_chunk_patcher_bytes(buf, r, false);
					tr-=r;
				}
				else
				{
					if(file_pos+tr>filesize)
					{
						tr=static_cast<unsigned int>(filesize-file_pos);
					}
					file_pos+=tr;
					tr=0;
				}				
			}			
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
