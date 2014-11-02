#pragma once

#include "../Interface/Types.h"

#include <string>

class IVHDFile
{
public:
	virtual ~IVHDFile() {}
	virtual bool Seek(_i64 offset)=0;
	virtual bool Read(char* buffer, size_t bsize, size_t &read)=0;
	virtual _u32 Write(const char *buffer, _u32 bsize)=0;
	virtual bool isOpen(void)=0;
	virtual uint64 getSize(void)=0;
	virtual uint64 usedSize(void)=0;
	virtual std::string getFilename(void)=0;
	virtual std::wstring getFilenameW(void)=0;
	virtual bool has_sector(void)=0;
	virtual unsigned int getBlocksize()=0;
	virtual bool finish() = 0;
	virtual bool trimUnused(_i64 fs_offset)=0;
	virtual bool syncBitmap(_i64 fs_offset)=0;
};
