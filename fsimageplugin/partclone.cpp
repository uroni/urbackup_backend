#include "partclone.h"
#include "../common/miniz.h"
#include <stdio.h>

#ifndef _WIN32
#define _popen popen
#define _pclose pclose
#endif

Partclone::Partclone(const std::string& pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback)
	: Filesystem(pDev, read_ahead, next_block_callback), bitmap(NULL)
{
	init();
	initReadahead(read_ahead, background_priority);
}

Partclone::Partclone(IFile* pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback)
	:Filesystem(pDev, next_block_callback), bitmap(NULL)
{
	init();
	initReadahead(read_ahead, background_priority);
}

Partclone::~Partclone()
{
	delete[] bitmap;
}

void Partclone::logFileChanges(std::string volpath, int64 min_size, char* fc_bitmap)
{
}

int64 Partclone::getBlocksize(void)
{
	return block_size;
}

int64 Partclone::getSize(void)
{
	return total_size;
}

const unsigned char* Partclone::getBitmap(void)
{
	return bitmap;
}

namespace
{
#define HEADER_ENDIAN_MAGIC 0xC0DE

	struct partclone_header
	{
		char magic[16];
		char partclone_version[14];
		char image_version[4];
		unsigned short endianess;
		char fs[16];
		uint64 device_size;
		uint64 total_blocks;
		uint64 used_blocks;
		uint64 used_bitmap;
		unsigned int block_size;
		unsigned int feature_size;
		unsigned short f_image_version;
		unsigned short cpu_bits;
		unsigned short checksum_mode;
		unsigned short checksum_size;
		unsigned int blocks_per_checksum;
		unsigned short reseed_checksum;
		unsigned short bitmap_mode;
		unsigned int crc;
	};

	bool read_in(FILE* in, char* buf, size_t buf_size)
	{
		do
		{
			size_t nread = fread(buf, 1, buf_size, in);
			if (nread == 0
				&& (feof(in)
					|| ferror(in)))
			{
				return false;
			}

			buf += nread;
			buf_size -= nread;
		} while (buf_size > 0);

		return true;
	}

	class AutoClose
	{
		FILE* in;
	public:
		AutoClose(FILE* in)
			: in(in)
		{}

		~AutoClose() {
			_pclose(in);
		}
	};
}

void Partclone::init()
{
	if (has_error)
		return;

	std::string cmd = "partclone -o - -c -s \"" + dev->getFilename() + "\"";

	FILE* in;
#ifdef __linux__
	in = _popen(cmd.c_str(), "re");
	if (!in) in = _popen(cmd.c_str(), "r");
#else
	in = _popen(cmd.c_str(), "r");
#endif

	if (in == NULL)
	{
		has_error = true;
		return;
	}

	AutoClose close_in(in);

	partclone_header header;
	if (!read_in(in, reinterpret_cast<char*>(&header), sizeof(header)))
	{
		has_error = true;
		return;
	}

	header.magic[15] = 0;

	unsigned int curr_crc = mz_crc32(mz_crc32(0, NULL, 0), reinterpret_cast<unsigned char*>(&header), sizeof(header) - sizeof(unsigned int));

	if (curr_crc != header.crc)
	{
		has_error = true;
		return;
	}

	if (std::string(header.magic) != "partclone-image")
	{
		has_error = true;
		return;
	}

	header.image_version[3] = 0;
	if (atoi(header.image_version) != 0x0002)
	{
		has_error = true;
		return;
	}

	if (header.endianess != HEADER_ENDIAN_MAGIC)
	{
		has_error = true;
		return;
	}

	if (header.bitmap_mode != 1)
	{
		has_error = true;
		return;
	}

	size_t bitmap_bytes = (header.total_blocks + 7) / 8;

	bitmap = new unsigned char[bitmap_bytes];

	if (!read_in(in, reinterpret_cast<char*>(bitmap), bitmap_bytes))
	{
		has_error = true;
		return;
	}

	unsigned int bitmap_crc;
	if (!read_in(in, reinterpret_cast<char*>(&bitmap_crc), sizeof(bitmap_crc)))
	{
		has_error=true;
		return;
	}

	curr_crc = mz_crc32(mz_crc32(0, NULL, 0), bitmap, bitmap_bytes);

	if (curr_crc != bitmap_crc)
	{
		has_error = true;
		return;
	}

	block_size = header.block_size;
	total_size = header.device_size;
}
