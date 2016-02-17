#ifndef CHUNK_PATCHER_H
#define CHUNK_PATCHER_H

#include "../Interface/File.h"

struct SPatchHeader
{
	_i64 patch_off;
	unsigned int patch_size;
};

class ExtentIterator;

class IChunkPatcherCallback
{
public:
	virtual void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed)=0;
	virtual void next_sparse_extent_bytes(const char *buf, size_t bsize) = 0;
};

class ChunkPatcher
{
public:
	ChunkPatcher(void);

	void setCallback(IChunkPatcherCallback *pCb);
	void setRequireUnchanged(bool b);
	void setUnchangedAlign(int64 a);
	void setWithSparse(bool b);
	bool ApplyPatch(IFile *file, IFile *patch, ExtentIterator* extent_iterator);
	_i64 getFilesize(void);

private:
	bool readNextValidPatch(IFile *patchf, _i64 &patchf_pos, SPatchHeader *patch_header, bool& has_read_error);
	void nextChunkPatcherBytes(int64 pos, const char *buf, size_t bsize, bool changed, bool sparse);
	void finishChunkPatcher(int64 pos);
	void finishSparse(int64 pos);
	_i64 filesize;

	IChunkPatcherCallback *cb;
	bool require_unchanged;
	bool with_sparse;

	int64 last_sparse_start;
	bool curr_only_zeros;
	bool curr_changed;
	int64 unchanged_align;
	std::vector<char> sparse_buf;
	int64 unchanged_align_start;
	int64 unchanged_align_end;
	bool last_unchanged;
};

#endif //CHUNK_PATCHER_H