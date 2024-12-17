#pragma once

#include "../Interface/File.h"
#include <memory>


class CachedFile : public IFsFile
{
public:

	CachedFile(IFsFile* backing_file);

	~CachedFile();

	// Inherited via IFile
	virtual std::string Read(_u32 tr, bool * has_error = NULL)
    {
        std::string ret = Read(currPos, tr, has_error);
        currPos += ret.size();
        return ret;
    }

	virtual std::string Read(int64 spos, _u32 tr, bool * has_error = NULL)
    {
        if(!flushCacheIfNecessary(spos, tr))
        {
            if(has_error)
            {
                *has_error = true;
            }
            return 0;
        }
        return backing_file->Read(spos, tr, has_error);
    }

	virtual _u32 Read(char * buffer, _u32 bsize, bool * has_error = NULL)
    {
        const _u32 ret = Read(currPos, buffer, bsize, has_error);
        currPos += ret;
        return ret;
    }

	virtual _u32 Read(int64 spos, char * buffer, _u32 bsize, bool * has_error = NULL)
    {
        if(!flushCacheIfNecessary(spos, bsize))
        {
            if(has_error)
            {
                *has_error = true;
            }
            return 0;
        }
        return readFromBuf(spos, buffer, bsize, has_error);
    }

	virtual _u32 Write(const std::string & tw, bool * has_error = NULL)
    {
        return Write(tw.data(), tw.size(), has_error);
    }

	virtual _u32 Write(int64 spos, const std::string & tw, bool * has_error = NULL)
    {
        return Write(spos, tw.data(), tw.size(), has_error);
    }

	virtual _u32 Write(const char * buffer, _u32 bsiz, bool * has_error = NULL)
    {
        const _u32 written =  Write(currPos, buffer, bsiz, has_error);
        currPos += written;
        return written;
    }

	virtual _u32 Write(int64 spos, const char * buffer, _u32 bsiz, bool * has_error = NULL)
    {
        invalidateReadBuf(spos, bsiz);
        if(!writeBuf(spos, buffer, bsiz))
        {
            if(has_error)
            {
                *has_error = true;
            }
            return 0;
        }
        return bsiz;
    }

	virtual bool Seek(_i64 spos)
    {
        currPos = spos;
        return backing_file->Seek(spos);
    }

	virtual _i64 Size(void)
    {
        return backing_file->Size();
    }

	virtual _i64 RealSize()
    {
        return backing_file->RealSize();
    }

	virtual bool PunchHole(_i64 spos, _i64 size)
    {
        return backing_file->PunchHole(spos, size);
    }

	virtual bool Sync()
    {
        if(!flushCache())
            return false;

        return backing_file->Sync();
    }

	virtual std::string getFilename(void)
    {
        return backing_file->getFilename();
    }

	virtual IFsFile::os_file_handle getOsHandle(bool release_handle = false)
    {
        return backing_file->getOsHandle(release_handle);
    }

	virtual void resetSparseExtentIter()
    {
        backing_file->resetSparseExtentIter();
    }

	virtual SSparseExtent nextSparseExtent()
    {
        return backing_file->nextSparseExtent();
    }

	virtual bool Resize(int64 new_size, bool set_sparse=true)
    {
        return backing_file->Resize(new_size, set_sparse);
    }

	virtual std::vector<SFileExtent> getFileExtents(int64 starting_offset, int64 block_size, bool& more_data)
    {
        return backing_file->getFileExtents(starting_offset, block_size, more_data);
    }

	virtual IVdlVolCache* createVdlVolCache()
    {
        return backing_file->createVdlVolCache();
    }

	virtual int64 getValidDataLength(IVdlVolCache* vol_cache)
    {
        return backing_file->getValidDataLength(vol_cache);
    }

private:
    bool flushCacheIfNecessary(int64 spos, _u32 tr);

    bool flushCache();

    bool writeBuf(int64 spos, const char* buf, _u32 bsize);

    _u32 readBuf(int64 spos, bool* has_error);

    _u32 readFromBuf(int64 spos, char* buffer, _u32 bsize, bool* has_error);

    void invalidateReadBuf(int64 spos, _u32 tr);

    std::vector<char> readCache;
    int64 readCacheOffset;
    _u32 readCacheSize;

    std::vector<char> writeCache;
    int64 writeCacheOffset;
    _u32 writeCacheSize;

    int64_t currPos;

	std::auto_ptr<IFsFile> backing_file;
};
