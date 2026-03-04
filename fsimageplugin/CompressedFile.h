#pragma once

#include <string>
#include <memory>

#include "../Interface/File.h"
#include "../Interface/Mutex.h"

class LRUMemCache;

struct SCacheItem
{
	SCacheItem()
		: buffer(NULL), offset(0)
	{
	}

	char* buffer;
	__int64 offset;
};

class ICacheEvictionCallback
{
private:
	virtual void evictFromLruCache(const SCacheItem& item) = 0;

	friend class LRUMemCache;
};

class CompressedFile : public IFsFile, public ICacheEvictionCallback
{
public:
	CompressedFile(std::string pFilename, int pMode, size_t n_threads);
	CompressedFile(IFile* file, bool openExisting, bool readOnly, size_t n_threads);
	~CompressedFile();

	virtual std::string Read(_u32 tr, bool *has_error=NULL);
	virtual std::string Read(int64 spos, _u32 tr, bool *has_error = NULL);
	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error=NULL);
	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error = NULL);
	virtual _u32 Write(const std::string &tw, bool *has_error=NULL);
	virtual _u32 Write(int64 spos, const std::string &tw, bool *has_error = NULL);
	virtual _u32 Write(const char* buffer, _u32 bsize, bool *has_error=NULL);
	virtual _u32 Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error = NULL);
	virtual bool Seek(_i64 spos);
	virtual _i64 Size(void);
	virtual _i64 RealSize();
	virtual bool PunchHole( _i64 spos, _i64 size );

	virtual std::string getFilename(void);

	virtual bool Sync();

	bool hasError();

	bool finish();

	bool hasNoMagic();

	void resetSparseExtentIter();
	SSparseExtent nextSparseExtent();
	bool Resize(int64 new_size, bool set_sparse);
	std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data);
	IVdlVolCache* createVdlVolCache();
	int64 getValidDataLength(IVdlVolCache* vol_cache);
	os_file_handle getOsHandle(bool release_handle);

private:
	void readHeader(bool *has_error);
	void readIndex(bool *has_error);
	bool fillCache(__int64 offset, bool errorMsg, bool *has_error);
	virtual void evictFromLruCache(const SCacheItem& item);
	void writeHeader();
	void writeIndex();
	void initCompressedBuffers(size_t n_init);
	char* getCompressedBuffer(size_t& compressed_buffer_idx);
	void returnCompressedBuffer(char* buf, size_t compressed_buffer_idx);


	_u32 readFromFile(int64 offset, char* buffer, _u32 bsize, bool *has_error);
	_u32 writeToFile(int64 offset, const char* buffer, _u32 bsize);
	

	__int64 filesize;
	__int64 index_offset;
	_u32 blocksize;

	__int64 currentPosition;

	std::vector<int64> blockOffsets;
	size_t numBlockOffsets;

	IFile* uncompressedFile;
	int64 uncompressedFileSize;
	
	std::auto_ptr<LRUMemCache> hotCache;

	//for reading
	std::vector<char> compressedBuffer;
	//for writing
	std::vector<char*> compressedBuffers;
	size_t compressedBufferSize;

	bool error;

	bool finished;

	bool readOnly;

	bool noMagic;

	std::auto_ptr<IMutex> mutex;

	size_t n_threads;
};
