#pragma once

#include <string>
#include <memory>

#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../cryptoplugin/IZlibDecompression.h"
#include "../cryptoplugin/IZlibCompression.h"
#include "LRUMemCache.h"


class CompressedFile : public IFile, public ICacheEvictionCallback
{
public:
	CompressedFile(std::wstring pFilename, int pMode);
	CompressedFile(IFile* file, bool openExisting);
	~CompressedFile();

	virtual std::string Read(_u32 tr);
	virtual _u32 Read(char* buffer, _u32 bsize);
	virtual _u32 Write(const std::string &tw);
	virtual _u32 Write(const char* buffer, _u32 bsize);
	virtual bool Seek(_i64 spos);
	virtual _i64 Size(void);

	virtual std::string getFilename(void);
	virtual std::wstring getFilenameW(void);

	bool hasError();

	bool finish();

private:
	void readHeader();
	void readIndex();
	bool fillCache(__int64 offset);
	virtual void evictFromLruCache(const SCacheItem& item);
	void writeHeader();
	void writeIndex();

	__int64 filesize;
	__int64 index_offset;
	_u32 blocksize;

	__int64 currentPosition;

	std::vector<__int64> blockOffsets;

	IFile* uncompressedFile;
	
	std::auto_ptr<LRUMemCache> hotCache;

	std::vector<char> compressedBuffer;

	IZlibDecompression* zlibDecompression;
	IZlibCompression* zlibCompression;

	bool error;

	bool finished;
};