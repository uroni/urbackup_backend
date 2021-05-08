#pragma once
#ifdef WITH_LZMA
#include <lzma.h>
#include "CompressEncrypt.h"

class LzmaCompressor : public ICompressor
{
public:
	LzmaCompressor();
	~LzmaCompressor();

	// Inherited via ICompressor
	virtual void setOut(char * next_out, size_t avail_out);
	virtual void setIn(char * next_in, size_t avail_in);
	virtual size_t getAvailOut();
	virtual size_t getAvailIn();
	virtual CompressResult compress(bool finish, int& code);
	virtual unsigned int getId();

private:
	lzma_stream strm;
};

class LzmaDecompressor : public IDecompressor
{
public:
	LzmaDecompressor();
	~LzmaDecompressor();

	// Inherited via ICompressor
	virtual void setOut(char * next_out, size_t avail_out);
	virtual void setIn(char * next_in, size_t avail_in);
	virtual size_t getAvailOut();
	virtual size_t getAvailIn();
	virtual DecompressResult decompress(int& code);

private:
	lzma_stream strm;
};
#endif //WITH_LZMA