#pragma once
#include <zlib.h>
#include "CompressEncrypt.h"

class CdZlibCompressor : public ICompressor
{
public:
	CdZlibCompressor(int compression_level, unsigned int compression_id);
	~CdZlibCompressor();

	// Inherited via ICompressor
	virtual void setOut(char * next_out, size_t avail_out);
	virtual void setIn(char * next_in, size_t avail_in);
	virtual size_t getAvailOut();
	virtual size_t getAvailIn();
	virtual CompressResult compress(bool finish, int& code);
	virtual unsigned int getId();

private:
	z_stream strm;
	unsigned int compression_id;
};

class CdZlibDecompressor : public IDecompressor
{
public:
	CdZlibDecompressor();
	~CdZlibDecompressor();

	// Inherited via ICompressor
	virtual void setOut(char * next_out, size_t avail_out);
	virtual void setIn(char * next_in, size_t avail_in);
	virtual size_t getAvailOut();
	virtual size_t getAvailIn();
	virtual DecompressResult decompress(int& code);

private:
	z_stream strm;
};