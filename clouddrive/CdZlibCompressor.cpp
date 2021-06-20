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

#include "CdZlibCompressor.h"

CdZlibCompressor::CdZlibCompressor(int compression_level, unsigned int compression_id)
	: compression_id(compression_id)
{
	memset(&strm, 0, sizeof(z_stream));

	if (deflateInit(&strm, compression_level) != Z_OK)
	{
		throw std::runtime_error("Error initializing compression stream");
	}
}

CdZlibCompressor::~CdZlibCompressor()
{
	deflateEnd(&strm);
}

void CdZlibCompressor::setOut(char * next_out, size_t avail_out)
{
	strm.next_out = reinterpret_cast<Bytef*>(next_out);
	strm.avail_out = avail_out;
}

void CdZlibCompressor::setIn(char * next_in, size_t avail_in)
{
	strm.next_in = reinterpret_cast<Bytef*>(next_in);
	strm.avail_in = avail_in;
}

size_t CdZlibCompressor::getAvailOut()
{
	return strm.avail_out;
}

size_t CdZlibCompressor::getAvailIn()
{
	return strm.avail_in;
}

CompressResult CdZlibCompressor::compress(bool finish, int & code)
{
	code = deflate(&strm, finish ? Z_FINISH : Z_NO_FLUSH);

	if (code == Z_OK)
	{
		return CompressResult_Ok;
	}
	else if (code == Z_STREAM_END)
	{
		return CompressResult_End;
	}
	else
	{
		if (code == Z_DATA_ERROR
			&& strm.msg != nullptr)
		{
			Server->Log(std::string("Zlib error: ") + strm.msg, LL_ERROR);
		}
		return CompressResult_Other;
	}

}

unsigned int CdZlibCompressor::getId()
{
	return compression_id;
}

CdZlibDecompressor::CdZlibDecompressor()
{
	memset(&strm, 0, sizeof(z_stream));

	if (inflateInit(&strm) != Z_OK)
	{
		throw std::runtime_error("Error initializing decompression stream");
	}
}

CdZlibDecompressor::~CdZlibDecompressor()
{
	inflateEnd(&strm);
}

void CdZlibDecompressor::setOut(char * next_out, size_t avail_out)
{
	strm.next_out = reinterpret_cast<Bytef*>(next_out);
	strm.avail_out = avail_out;
}

void CdZlibDecompressor::setIn(char * next_in, size_t avail_in)
{
	strm.next_in = reinterpret_cast<Bytef*>(next_in);
	strm.avail_in = avail_in;
}

size_t CdZlibDecompressor::getAvailOut()
{
	return strm.avail_out;
}

size_t CdZlibDecompressor::getAvailIn()
{
	return strm.avail_in;
}

DecompressResult CdZlibDecompressor::decompress(int & code)
{
	code = inflate(&strm, Z_NO_FLUSH);

	if (code == Z_OK)
	{
		return DecompressResult_Ok;
	}
	else if (code == Z_STREAM_END)
	{
		return DecompressResult_End;
	}
	else
	{
		if (code == Z_DATA_ERROR)
		{
			if (strm.msg != nullptr)
			{
				Server->Log(std::string("Error while decompressing Z_DATA_ERROR: ") + strm.msg, LL_ERROR);
			}
		}
		return DecompressResult_Other;
	}
}
