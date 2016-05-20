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
};
