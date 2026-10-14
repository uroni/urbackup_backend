#pragma once

#include "../Interface/Types.h"

#include <string>

class ITrimCallback
{
public:
	virtual void trimmed(_i64 trim_start, _i64 trim_stop) = 0;
};

class IVHDWriteCallback
{
public:
	virtual bool writeVHD(uint64 pos, char *buf, unsigned int bsize) = 0;
	virtual bool emptyVHDBlock(int64 empty_start, int64 empty_end) = 0;
};

class IVHDFile
{
public:
	virtual ~IVHDFile() {}
	virtual bool Seek(_i64 offset)=0;
	virtual bool Read(char* buffer, size_t bsize, size_t &read)=0;
	virtual _u32 Write(const char *buffer, _u32 bsize, bool *has_error=NULL)=0;
	virtual bool isOpen(void)=0;
	virtual uint64 getSize(void)=0;
	virtual uint64 usedSize(void)=0;
	virtual std::string getFilename(void)=0;
	virtual bool has_sector(_i64 sector_size=-1)=0;
	virtual bool this_has_sector(_i64 sector_size=-1)=0;
	virtual unsigned int getBlocksize()=0;
	virtual bool finish() = 0;
	virtual bool trimUnused(_i64 fs_offset, _i64 trim_blocksize, ITrimCallback* trim_callback)=0;
	virtual bool syncBitmap(_i64 fs_offset)=0;
	virtual bool makeFull(_i64 fs_offset, IVHDWriteCallback* write_callback)=0;
	virtual bool setUnused(_i64 unused_start, _i64 unused_end) = 0;
	virtual bool setBackingFileSize(_i64 fsize) = 0;
};

//Returns true if any block of [start, start+len) is present in the image (has_sector()). Changes the position
inline bool vhd_range_has_sector(IVHDFile* vhdfile, _i64 start, _i64 len)
{
	const _i64 blocksize = vhdfile->getBlocksize();
	if (blocksize <= 0)
	{
		return true;
	}

	for (_i64 bpos = start; bpos < start + len; bpos = (bpos / blocksize + 1)*blocksize)
	{
		if (!vhdfile->Seek(bpos)
			|| vhdfile->has_sector())
		{
			return true;
		}
	}
	return false;
}
