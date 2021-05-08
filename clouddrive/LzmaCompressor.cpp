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

#ifdef WITH_LZMA
#include "LzmaCompressor.h"

LzmaCompressor::LzmaCompressor()
{
	memset(&strm, 0, sizeof(lzma_stream));

	lzma_ret ret = lzma_easy_encoder(&strm, 5, LZMA_CHECK_CRC32);

	if (ret != LZMA_OK)
	{
		Server->Log("Error initializing LZMA", LL_ERROR);
		throw std::runtime_error("Error initializing LZMA");
	}
}

LzmaCompressor::~LzmaCompressor()
{
	lzma_end(&strm);
}

void LzmaCompressor::setOut(char * next_out, size_t avail_out)
{
	strm.next_out = reinterpret_cast<uint8_t*>(next_out);
	strm.avail_out = avail_out;
}

void LzmaCompressor::setIn(char * next_in, size_t avail_in)
{
	strm.next_in = reinterpret_cast<uint8_t*>(next_in);
	strm.avail_in = avail_in;
}

size_t LzmaCompressor::getAvailOut()
{
	return strm.avail_out;
}

size_t LzmaCompressor::getAvailIn()
{
	return strm.avail_in;
}

CompressResult LzmaCompressor::compress(bool finish, int& code)
{
	lzma_ret ret = lzma_code(&strm, finish ? LZMA_FINISH : LZMA_RUN);

	code = ret;

	if (ret == LZMA_OK)
	{
		return CompressResult_Ok;
	}
	else if(ret== LZMA_STREAM_END)
	{
		return CompressResult_End;
	}
	else
	{
		return CompressResult_Other;
	}
}

unsigned int LzmaCompressor::getId()
{
	return CompressionLzma5;
}

LzmaDecompressor::LzmaDecompressor()
{
	memset(&strm, 0, sizeof(lzma_stream));

	lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, 0);

	if (ret != LZMA_OK)
	{
		Server->Log("Error initializing LZMA", LL_ERROR);
		throw std::runtime_error("Error initializing LZMA");
	}
}

LzmaDecompressor::~LzmaDecompressor()
{
	lzma_end(&strm);
}

void LzmaDecompressor::setOut(char * next_out, size_t avail_out)
{
	strm.next_out = reinterpret_cast<uint8_t*>(next_out);
	strm.avail_out = avail_out;
}

void LzmaDecompressor::setIn(char * next_in, size_t avail_in)
{
	strm.next_in = reinterpret_cast<uint8_t*>(next_in);
	strm.avail_in = avail_in;
}

size_t LzmaDecompressor::getAvailOut()
{
	return strm.avail_out;
}

size_t LzmaDecompressor::getAvailIn()
{
	return strm.avail_in;
}

DecompressResult LzmaDecompressor::decompress(int & code)
{
	lzma_ret ret = lzma_code(&strm, LZMA_RUN);

	code = ret;

	if (ret == LZMA_OK)
	{
		return DecompressResult_Ok;
	}
	else if (ret == LZMA_STREAM_END)
	{
		return DecompressResult_End;
	}
	else
	{
		return DecompressResult_Other;
	}
}
#endif //WITH_LZMA