#include "CompressedFile.h"
#include "../stringtools.h"
#include <assert.h>
#include <memory>
#include <algorithm>

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.c"

const size_t c_cacheBuffersize = 2*1024*1024;
const size_t c_ncacheItems = 5;
const char headerMagic[] = "URBACKUP COMPRESSED FILE#1.0";
const _u32 mode_none = 0;
const _u32 mode_zlib = 1;
const size_t c_header_size = sizeof(headerMagic) + sizeof(__int64) + sizeof(__int64) + sizeof(_u32);


CompressedFile::CompressedFile( std::wstring pFilename, int pMode )
	: hotCache(NULL), error(false), currentPosition(0),
	  finished(false), filesize(0), noMagic(false)
{
	uncompressedFile = Server->openFile(pFilename, pMode);

	if(uncompressedFile==NULL)
	{
		Server->Log(L"Could not open compressed file \""+pFilename+L"\"", LL_ERROR);
		error=true;
		return;
	}

	if(pMode == MODE_READ ||
		pMode == MODE_RW )
	{
		readOnly=true;
		readHeader();
	}
	else
	{
		readOnly=false;
		blocksize = c_cacheBuffersize;
		writeHeader();
		hotCache.reset(new LRUMemCache(blocksize, c_ncacheItems));
		compressedBuffer.resize(mz_compressBound(static_cast<mz_ulong>(blocksize)));
	}

	if(hotCache.get())
	{
		hotCache->setCacheEvictionCallback(this);
	}
}

CompressedFile::CompressedFile(IFile* file, bool openExisting, bool readOnly)
	: hotCache(NULL), error(false), currentPosition(0),
	finished(false), uncompressedFile(file), filesize(0), readOnly(readOnly),
	noMagic(false)
{
	if(openExisting)
	{
		readHeader();
	}
	else
	{
		blocksize = c_cacheBuffersize;
		writeHeader();
		hotCache.reset(new LRUMemCache(blocksize, c_ncacheItems));
		compressedBuffer.resize(mz_compressBound(static_cast<mz_ulong>(blocksize)));
	}
	if(hotCache.get()!=NULL)
	{
		hotCache->setCacheEvictionCallback(this);
	}
}

CompressedFile::~CompressedFile()
{
	if(!finished)
	{
		finish();
	}

	delete uncompressedFile;
}

bool CompressedFile::hasError()
{
	return error;
}

void CompressedFile::readHeader()
{	
	if(!uncompressedFile->Seek(0))
	{
		Server->Log("Error while seeking to header", LL_ERROR);
		error=true;
		return;
	}
	std::string header;
	header.resize(c_header_size);
	if(readFromFile(&header[0], c_header_size)!=c_header_size)
	{
		Server->Log("Error while reading compressed file header", LL_ERROR);
		error=true;
		return;
	}

	if(!next(header, 0, headerMagic))
	{
		Server->Log("Magic in header not found for compressed file", LL_ERROR);
		error=true;
		noMagic=true;
		return;
	}

	memcpy(&index_offset, header.data()+sizeof(headerMagic), sizeof(index_offset));
	memcpy(&filesize, header.data()+sizeof(headerMagic)+sizeof(index_offset), sizeof(filesize));
	memcpy(&blocksize, header.data()+sizeof(headerMagic)+sizeof(index_offset)+sizeof(filesize), sizeof(blocksize));

	index_offset = little_endian(index_offset);
	filesize = little_endian(filesize);
	blocksize = little_endian(blocksize);

	hotCache.reset(new LRUMemCache(blocksize, c_ncacheItems));

	readIndex();
}

void CompressedFile::readIndex()
{
	if(!uncompressedFile->Seek(index_offset))
	{
		Server->Log("Error while seeking to compressed file block index", LL_ERROR);
		error=true;
		return;
	}

	size_t nOffsetItems = filesize/blocksize + ((filesize%blocksize!=0)?1:0);

	blockOffsets.resize(nOffsetItems);

	if(readFromFile(reinterpret_cast<char*>(&blockOffsets[0]), static_cast<_u32>(sizeof(__int64)*nOffsetItems))
		!=sizeof(__int64)*nOffsetItems)
	{
		Server->Log("Error while reading block offsets", LL_ERROR);
		error=true;
		return;
	}

	if(is_big_endian())
	{
		for(size_t i=0;i<blockOffsets.size();++i)
		{
			blockOffsets[i] = little_endian(blockOffsets[i]);
		}
	}
}

bool CompressedFile::Seek( _i64 spos )
{
	assert(!finished);
	currentPosition = spos;
	return true;
}

_u32 CompressedFile::Read( char* buffer, _u32 bsize )
{
	assert(!finished);

	size_t cacheSize;
	char* cachePtr = hotCache->get(currentPosition, cacheSize);

	if(cachePtr == NULL)
	{
		if(!fillCache(currentPosition, !readOnly))
		{
			return 0;
		}

		cachePtr = hotCache->get(currentPosition, cacheSize);

		if(cachePtr==NULL)
		{
			return 0;
		}
	}

	size_t canRead = bsize;
	if(cacheSize<canRead)
		canRead = cacheSize;
	if(currentPosition+canRead>filesize)
		canRead = filesize-currentPosition;

	memcpy(buffer, cachePtr, canRead);

	currentPosition+=canRead;

	if(canRead<bsize)
	{
		return static_cast<_u32>(canRead) + Read(buffer + canRead, bsize-static_cast<_u32>(canRead));
	}

	return static_cast<_u32>(canRead);
}

std::string CompressedFile::Read( _u32 tr )
{
	assert(!finished);

	if(tr==0)
		return std::string();

	std::string ret;
	ret.resize(tr);

	if(Read(&ret[0], static_cast<_u32>(ret.size()))!=tr)
	{
		return std::string();
	}

	return ret;
}

bool CompressedFile::fillCache( __int64 offset, bool errorMsg)
{
	size_t block = static_cast<size_t>(offset/blocksize);

	if(block>=blockOffsets.size() || blockOffsets[block]==-1)
	{
		if(errorMsg)
		{
			Server->Log("Block "+nconvert(block)+" to read not found in block index", LL_ERROR);
		}
		return false;
	}

	char* buf = hotCache->create(offset);

	if(error)
	{
		return false;
	}

	const __int64 blockDataOffset = blockOffsets[block];	

	if(!uncompressedFile->Seek(blockDataOffset))
	{
		Server->Log("Error while seeking to offset "+nconvert(blockDataOffset)+" to read compressed data", LL_ERROR);
		return false;
	}

	char blockheaderBuf[2*sizeof(_u32)];
	if(readFromFile(blockheaderBuf, sizeof(blockheaderBuf))!=sizeof(blockheaderBuf))
	{
		Server->Log("Error while reading block header", LL_ERROR);
		return false;
	}

	_u32 compressedSize;
	memcpy(&compressedSize, blockheaderBuf, sizeof(compressedSize));
	compressedSize = little_endian(compressedSize);
	_u32 mode;
	memcpy(&mode, blockheaderBuf + sizeof(compressedSize), sizeof(mode));
			
	if(mode==mode_none)
	{
		if(compressedSize>blocksize)
		{
			Server->Log("Blocksize too large at offset "+nconvert(blockDataOffset)+" ("+nconvert(compressedSize)+" bytes)", LL_ERROR);
			return false;
		}

		if(readFromFile(buf, compressedSize)!=compressedSize)
		{
			Server->Log("Error while reading uncompressed data from "+nconvert(blockDataOffset)+" ("+nconvert(compressedSize)+" bytes)", LL_ERROR);
			return false;
		}

		return true;
	}
	else
	{
		if(compressedBuffer.size()<compressedSize)
		{
			compressedBuffer.resize(compressedSize);
		}	

		if(readFromFile(&compressedBuffer[0], compressedSize)!=compressedSize)
		{
			Server->Log("Error while reading compressed data from "+nconvert(blockDataOffset)+" ("+nconvert(compressedSize)+" bytes)", LL_ERROR);
			return false;
		}
	}
	
	mz_ulong rdecomp;
	if(mode==mode_zlib)
	{
		rdecomp = blocksize;
		int rc = mz_uncompress(reinterpret_cast<unsigned char*>(buf), &rdecomp,
			reinterpret_cast<const unsigned char*>(compressedBuffer.data()), static_cast<mz_ulong>(compressedSize));

		if(rc != MZ_OK)
		{
			Server->Log("Error while decompressing file. Error code "+nconvert(rc), LL_ERROR);
			return false;
		}
	}
	

	if(rdecomp!=blocksize && offset+blocksize<filesize)
	{
		Server->Log("Did not receive enough bytes from compressed stream. Expected "+nconvert(blocksize)+" received "+nconvert((size_t)rdecomp), LL_ERROR);
		return false;
	}

	return true;
}

_u32 CompressedFile::Write( const char* buffer, _u32 bsize )
{
	assert(!finished);

	_u32 maxWrite = static_cast<_u32>(((currentPosition/blocksize)+1)*blocksize - currentPosition);
	_u32 write = bsize;
	if(maxWrite<bsize)
	{
		write=maxWrite;
	}

	size_t cacheSize;
	char* cachePtr = hotCache->get(currentPosition, cacheSize);

	if(cachePtr==NULL)
	{
		fillCache(currentPosition, false);
	}

	if(error)
	{
		error=false;
		return 0;
	}

	hotCache->put(currentPosition, buffer, write);
	currentPosition += write;

	if(error)
	{
		error=false;
		return 0;
	}

	if(currentPosition>filesize)
	{
		filesize=currentPosition;
	}

	if(maxWrite<bsize)
	{
		return write + Write(buffer + write, bsize-write);
	}

	return write;
}

_u32 CompressedFile::Write( const std::string &tw )
{
	return Write(tw.data(), static_cast<_u32>(tw.size()));
}

void CompressedFile::evictFromLruCache( const SCacheItem& item )
{
	if(readOnly)
		return;

	__int64 blockOffset = uncompressedFile->Size();
	if(!uncompressedFile->Seek(blockOffset))
	{
		error=true;
		Server->Log("Error while seeking to end of file while before writing compressed data", LL_ERROR);
		return;
	}

	mz_ulong compBytes = static_cast<mz_ulong>(compressedBuffer.size());
	int rc = mz_compress(reinterpret_cast<unsigned char*>(compressedBuffer.data()), &compBytes,
		reinterpret_cast<const unsigned char*>(item.buffer), blocksize);

	if(rc!=MZ_OK)
	{
		error=true;
		Server->Log("Error while compressing data. Error code: "+nconvert(rc), LL_ERROR);
		return;
	}

	char blockheaderBuf[2*sizeof(_u32)];
	_u32 compBytesEndian = little_endian(static_cast<_u32>(compBytes));
	_u32 mode = mode_zlib;
	_u32 modeEndian = little_endian(mode);

	memcpy(blockheaderBuf, &compBytesEndian, sizeof(compBytesEndian));
	memcpy(blockheaderBuf+sizeof(compBytesEndian), &modeEndian, sizeof(modeEndian));

	if(writeToFile(blockheaderBuf, sizeof(blockheaderBuf))!=sizeof(blockheaderBuf))
	{
		error=true;
		Server->Log("Error while writing blockheader to compressed file", LL_ERROR);
		return;
	}

	if(writeToFile(compressedBuffer.data(), static_cast<_u32>(compBytes))!=static_cast<_u32>(compBytes))
	{
		error=true;
		Server->Log("Error while writing compressed data to file", LL_ERROR);
		return;
	}

	size_t blockIdx = item.offset/blocksize;

	const size_t numBlockOffsets = blockOffsets.size();
	if(blockOffsets.size()<=blockIdx)
	{
		blockOffsets.resize(blockIdx+1);

		if(blockIdx-numBlockOffsets>0)
		{
			std::fill(blockOffsets.begin()+numBlockOffsets+1, blockOffsets.begin()+blockIdx, -1);
		}
	}

	blockOffsets[blockIdx] = blockOffset;
}

void CompressedFile::writeHeader()
{
	char header[c_header_size];
	char* cptr = header;
	memcpy(cptr, headerMagic, sizeof(headerMagic));
	cptr+=sizeof(headerMagic);
	__int64 indexOffsetEndian = little_endian(index_offset);
	memcpy(cptr, &indexOffsetEndian, sizeof(indexOffsetEndian));
	cptr+=sizeof(indexOffsetEndian);
	__int64 filesizeEndian = little_endian(filesize);
	memcpy(cptr, &filesizeEndian, sizeof(filesizeEndian));
	cptr+=sizeof(filesizeEndian);
	_u32 blocksizeEndian = little_endian(blocksize);
	memcpy(cptr, &blocksize, sizeof(blocksizeEndian));

	uncompressedFile->Seek(0);

	if(writeToFile(header, c_header_size)!=c_header_size)
	{
		Server->Log("Error writing header to compressed file");
		error=true;
	}
}

void CompressedFile::writeIndex()
{
	index_offset = uncompressedFile->Size();
	if(!uncompressedFile->Seek(index_offset))
	{
		error=true;
		Server->Log("Error while seeking to end of file while before writing index", LL_ERROR);
		return;
	}

	_u32 nOffsetBytes = static_cast<_u32>(sizeof(__int64)*blockOffsets.size());
	if(writeToFile(reinterpret_cast<char*>(&blockOffsets[0]), nOffsetBytes)!=nOffsetBytes)
	{
		error=true;
		Server->Log("Error while writing compressed file index", LL_ERROR);
		return;
	}
}

bool CompressedFile::finish()
{
	assert(!finished);

	if(hotCache.get())
	{
		hotCache->clear();
	}

	if(!readOnly)
	{
		writeIndex();
		writeHeader();
	}

	if(!error)
	{
		finished = true;
		return true;
	}
	else
	{
		error=false;
		return false;
	}
	
}

_i64 CompressedFile::Size( void )
{
	return filesize;
}

std::string CompressedFile::getFilename( void )
{
	return uncompressedFile->getFilename();
}

std::wstring CompressedFile::getFilenameW( void )
{
	return uncompressedFile->getFilenameW();
}

_u32 CompressedFile::readFromFile(char* buffer, _u32 bsize)
{
	_u32 read = 0;
	do 
	{
		_u32 rc = uncompressedFile->Read(buffer+read, bsize-read);
		if(rc<=0)
		{
			return read;
		}
		read+=rc;
	} while (read<bsize);

	return read;
}

_u32 CompressedFile::writeToFile(const char* buffer, _u32 bsize)
{
	_u32 written = 0;
	do
	{
		_u32 w = uncompressedFile->Write(buffer + written, bsize-written);
		if(w<=0)
		{
			return written;
		}
		written += w;
	} while (written<bsize);

	return written;
}

bool CompressedFile::hasNoMagic()
{
	return noMagic;
}
