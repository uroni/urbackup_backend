#include "ChunkPatcher.h"

ChunkPatcher::ChunkPatcher(void)
	: cb(NULL)
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

	char buf[4096];

	if(patch->Read((char*)&filesize, sizeof(_i64))!=sizeof(_i64))
	{
		return false;
	}

	patchf_pos+=sizeof(_i64);

	SPatchHeader next_header;
	next_header.patch_off=-1;
	bool has_header=true;
	for(_i64 file_pos=0,size=file->Size();file_pos<size || has_header;)
	{
		if(has_header && next_header.patch_off==-1)
		{
			has_header=readNextValidPatch(patch, patchf_pos, &next_header);
		}

		unsigned int tr=4096;
		if(next_header.patch_off!=-1)
		{
			_i64 hoff=next_header.patch_off-file_pos;
			if(hoff>=0 && hoff<tr)
				tr=(unsigned int)hoff;
		}

		if(file_pos>=size || tr==0)
		{
			file_pos+=next_header.patch_size;

			while(next_header.patch_size>0)
			{
				_u32 r=patch->Read((char*)buf, (std::min)((unsigned int)4096, next_header.patch_size));
				patchf_pos+=r;
				cb->next_chunk_patcher_bytes(buf, r, true);
				next_header.patch_size-=r;
			}
			file->Seek(file_pos);
			next_header.patch_off=-1;
		}
		else if(file_pos<size)
		{
			while(tr>0 && file_pos<size)
			{
				_u32 r=file->Read((char*)buf, tr);
				file_pos+=r;
				cb->next_chunk_patcher_bytes(buf, r, false);
				tr-=r;
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