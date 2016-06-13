#pragma once
#include "../Interface/Object.h"

class IFilesystem;

class fs_buffer
{
public:
	fs_buffer(IFilesystem* fs, char* buf)
		: fs(fs), buf(buf)
	{ }
	
	~fs_buffer();

	char* get() {
		return buf;
	}

private:

	fs_buffer(const fs_buffer& other) 
		: fs(other.fs), buf(other.buf)
	{
	}

	fs_buffer& operator=(const fs_buffer& other) {
		fs_buffer temp(other);
		temp.swap(*this);
		return *this;
	}

	void swap(fs_buffer& other)
	{
		std::swap(this->fs, other.fs);
		std::swap(this->buf, other.buf);
	}


	IFilesystem* fs;
	char* buf;
};

class IReadOnlyBitmap : public IObject
{
public:
	virtual int64 getBlocksize() = 0;
	virtual bool hasBlock(int64 block) = 0;
	virtual bool hasError(void) = 0;
};

class IFilesystem : public IReadOnlyBitmap
{
public:
	virtual int64 getBlocksize(void)=0;
	virtual int64 getSize(void)=0;
	virtual const unsigned char *getBitmap(void)=0;

	virtual bool hasBlock(int64 pBlock)=0;
	virtual char* readBlock(int64 pBlock)=0;
	virtual std::vector<int64> readBlocks(int64 pStartBlock, unsigned int n,
		const std::vector<char*>& buffers, unsigned int buffer_offset=0)=0;
	virtual bool hasError(void)=0;
	virtual int64 calculateUsedSpace(void)=0;
	virtual void releaseBuffer(char* buf)=0;

	virtual void shutdownReadahead()=0;
};

class FsShutdownHelper
{
public:
	FsShutdownHelper(IFilesystem* fs)
		: fs(fs) {}

	FsShutdownHelper()
		: fs(NULL) {}

	void reset(IFilesystem* pfs) {
		fs=pfs;
	}

	~FsShutdownHelper() {
		if(fs!=NULL) fs->shutdownReadahead();
	}	
private:
	IFilesystem* fs;
};

inline fs_buffer::~fs_buffer()
{
	if(buf!=NULL)
	{
		fs->releaseBuffer(buf);
	}
}
