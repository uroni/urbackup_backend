#ifndef CHUNK_PATCHER_H
#define CHUNK_PATCHER_H

#include "../Interface/File.h"

struct SPatchHeader
{
	_i64 patch_off;
	unsigned int patch_size;
};

class IChunkPatcherCallback
{
public:
	virtual void next_chunk_patcher_bytes(const char *buf, size_t bsize)=0;
};

class ChunkPatcher
{
public:
	ChunkPatcher(void);

	void setCallback(IChunkPatcherCallback *pCb);
	bool ApplyPatch(IFile *file, IFile *patch);
	_i64 getFilesize(void);

private:
	bool readNextValidPatch(IFile *patchf, _i64 patchf_pos, SPatchHeader *patch_header);
	_i64 filesize;

	IChunkPatcherCallback *cb;
};

#endif //CHUNK_PATCHER_H