#pragma once
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include "CompressEncrypt.h"

class CdZstdCompressor : public ICompressor
{
public:
	CdZstdCompressor(int compression_level, unsigned int compression_id);
	~CdZstdCompressor();

	// Inherited via ICompressor
	virtual void setOut(char * next_out, size_t avail_out);
	virtual void setIn(char * next_in, size_t avail_in);
	virtual size_t getAvailOut();
	virtual size_t getAvailIn();
	virtual CompressResult compress(bool finish, int& code);
	virtual unsigned int getId();

private:
	ZSTD_CCtx* cctx;
	unsigned int compression_id;

	ZSTD_inBuffer inBuffer;
	ZSTD_outBuffer outBuffer;
};

class CdZstdDecompressor : public IDecompressor
{
public:
	CdZstdDecompressor();
	~CdZstdDecompressor();

	// Inherited via ICompressor
	virtual void setOut(char * next_out, size_t avail_out);
	virtual void setIn(char * next_in, size_t avail_in);
	virtual size_t getAvailOut();
	virtual size_t getAvailIn();
	virtual DecompressResult decompress(int& code);

private:
	ZSTD_DStream* dstream;

	ZSTD_inBuffer inBuffer;
	ZSTD_outBuffer outBuffer;
};
