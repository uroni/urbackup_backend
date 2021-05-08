/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "CdZstdCompressor.h"

CdZstdCompressor::CdZstdCompressor(int compression_level, unsigned int compression_id)
	: cctx(ZSTD_createCCtx()), compression_id(compression_id), inBuffer(), outBuffer()
{
	if (cctx == nullptr)
	{
		abort();
	}
	size_t err = ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compression_level);

	if (ZSTD_isError(err))
	{
		Server->Log(std::string("Error setting zstd compression level. ") + ZSTD_getErrorName(err), LL_ERROR);
	}

	err = ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);

	if (ZSTD_isError(err))
	{
		Server->Log(std::string("Error setting zstd compression flag. ") + ZSTD_getErrorName(err), LL_ERROR);
	}
}

CdZstdCompressor::~CdZstdCompressor()
{
	ZSTD_freeCCtx(cctx);
}

void CdZstdCompressor::setOut(char * next_out, size_t avail_out)
{
	outBuffer.dst = next_out;
	outBuffer.pos = 0;
	outBuffer.size= avail_out;
}

void CdZstdCompressor::setIn(char * next_in, size_t avail_in)
{
	inBuffer.src = next_in;
	inBuffer.size = avail_in;
	inBuffer.pos = 0;
}

size_t CdZstdCompressor::getAvailOut()
{
	return outBuffer.size - outBuffer.pos;
}

size_t CdZstdCompressor::getAvailIn()
{
	return inBuffer.size - inBuffer.pos;
}

CompressResult CdZstdCompressor::compress(bool finish, int & code)
{
	size_t remaining;
	remaining = ZSTD_compressStream2(cctx, &outBuffer, &inBuffer, finish ? ZSTD_e_end : ZSTD_e_continue);

	if (finish
		&& remaining == 0)
	{
		return CompressResult_End;
	}
	else if (ZSTD_isError(remaining))
	{
		Server->Log(std::string("Error compressing zstd: ") + ZSTD_getErrorName(remaining), LL_ERROR);
		code = static_cast<int>(remaining);
		return CompressResult_Other;
	}
	else
	{
		return CompressResult_Ok;
	}
}

unsigned int CdZstdCompressor::getId()
{
	return compression_id;
}

CdZstdDecompressor::CdZstdDecompressor()
	: dstream(ZSTD_createDStream())
{
	if (dstream == nullptr)
		abort();

	size_t rc =ZSTD_initDStream(dstream);

	if (ZSTD_isError(rc))
	{
		Server->Log(std::string("Error initializing zstd dstream ") + ZSTD_getErrorName(rc), LL_ERROR);
		abort();
	}
}

CdZstdDecompressor::~CdZstdDecompressor()
{
	ZSTD_freeDStream(dstream);
}

void CdZstdDecompressor::setOut(char * next_out, size_t avail_out)
{
	outBuffer.dst = next_out;
	outBuffer.size = avail_out;
	outBuffer.pos = 0;
}

void CdZstdDecompressor::setIn(char * next_in, size_t avail_in)
{
	inBuffer.src = next_in;
	inBuffer.size = avail_in;
	inBuffer.pos = 0;
}

size_t CdZstdDecompressor::getAvailOut()
{
	return outBuffer.size - outBuffer.pos;
}

size_t CdZstdDecompressor::getAvailIn()
{
	return inBuffer.size - inBuffer.pos;
}

DecompressResult CdZstdDecompressor::decompress(int & code)
{
	size_t rc = ZSTD_decompressStream(dstream, &outBuffer, &inBuffer);

	if (ZSTD_isError(rc))
	{
		Server->Log(std::string("Error decompressing zstd: ") + ZSTD_getErrorName(rc), LL_ERROR);
		code = static_cast<int>(rc);
		return DecompressResult_Other;
	}

	if (rc == 0)
	{
		return DecompressResult_End;
	}

	return DecompressResult_Ok;
}
