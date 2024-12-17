#include "CachedFile.h"

CachedFile::CachedFile(IFsFile* backing_file)
    : backing_file(backing_file), writeCacheOffset(0), writeCacheSize(0), currPos(0), readCacheOffset(0), readCacheSize(0)
{
    writeCache.resize(1 * 1024 * 1024);
    readCache.resize(512 * 1024);
}

CachedFile::~CachedFile()
{
    if (!flushCache())
    {
        Server->Log("Error flushing cache of " + backing_file->getFilename() + " on destroy", LL_ERROR);
    }
}

bool CachedFile::flushCacheIfNecessary(int64 spos, _u32 tr)
{
    if (writeCacheSize == 0)
        return true;

    if ((spos >= writeCacheOffset && spos < writeCacheOffset + writeCacheSize)
        || (spos + tr >= writeCacheOffset && spos + tr < writeCacheOffset + writeCacheSize))
        return flushCache();

    return true;
}

bool CachedFile::flushCache()
{
    while (writeCacheSize > 0)
    {
        const _u32 written = backing_file->Write(writeCacheOffset, writeCache.data(), writeCacheSize);
        const bool writeOk = written>0;
        writeCacheSize -= written;
        writeCacheOffset += written;
        if (!writeOk)
            return false;
    }

    return true;
}

bool CachedFile::writeBuf(int64 spos, const char* buf, _u32 bsize)
{
    // If the write is larger than the cache, write directly
    if (bsize>writeCache.size())
    {
        if (!flushCacheIfNecessary(spos, bsize))
        {
            return false;
        }
        const _u32 written = backing_file->Write(spos, buf, bsize);
        return written == bsize;
    }

    if (writeCacheOffset + writeCacheSize == spos)
    {
        if (writeCacheSize + bsize <= writeCache.size())
        {
            memcpy(writeCache.data() + writeCacheSize, buf, bsize);
            writeCacheSize += bsize;
        }
        else
        {
            if (!flushCache())
                return false;

            writeCacheOffset = spos;
            writeCacheSize = bsize;
            memcpy(writeCache.data(), buf, bsize);
        }
    }
    else
    {
        if (!flushCache())
            return false;

        writeCacheOffset = spos;
        writeCacheSize = bsize;
        memcpy(writeCache.data(), buf, bsize);
    }

    return true;
}

_u32 CachedFile::readBuf(int64 spos, bool* has_error)
{
    const _u32 read = backing_file->Read(spos, readCache.data(), readCache.size(), has_error);
    readCacheOffset = spos;
    readCacheSize = read;
    return read;
}

_u32 CachedFile::readFromBuf(int64 spos, char* buffer, _u32 bsize, bool* has_error)
{
    if (bsize > readCache.size())
        return 0;

    if (readCacheSize > 0 && spos >= readCacheOffset && readCacheOffset + readCacheSize < spos)
    {
        const int64 cacheOff = spos - readCacheOffset;
        const _u32 tr = (std::min)(readCacheSize - (_u32)cacheOff, bsize);
        memcpy(buffer, readCache.data() + cacheOff, tr);

        spos += tr;
        buffer += tr;
        bsize -= tr;
    }

    const _u32 read = readBuf(spos, has_error);
    const _u32 tr = (std::min)(read, bsize);

    memcpy(buffer, readCache.data(), tr);

    return tr;
}

void CachedFile::invalidateReadBuf(int64 spos, _u32 tr)
{
    if (readCacheSize == 0)
        return;

    if ((spos >= readCacheOffset && spos < readCacheOffset + readCacheSize)
        || (spos + tr >= readCacheOffset && spos + tr < readCacheOffset + readCacheSize))
    {
        readCacheSize = 0;
    }
}

