#include "partclone.h"
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../common/miniz.h"
#include <stdio.h>
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"

#ifndef _WIN32
#define _popen popen
#define _pclose pclose
#endif

#if defined(__ANDROID__)
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#define fsblkcnt64_t fsblkcnt_t
#include "../urbackupcommon/android_popen.h"
#endif

Partclone::Partclone(const std::string& pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback)
	: Filesystem(pDev, read_ahead, next_block_callback), bitmap(nullptr)
{
	init();
	initReadahead(read_ahead, background_priority);
}

Partclone::Partclone(IFile* pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback)
	:Filesystem(pDev, next_block_callback), bitmap(nullptr)
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

#pragma pack(push)
#pragma pack(1)
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
		unsigned char reseed_checksum;
		unsigned char bitmap_mode;
		unsigned int crc;
	};
#pragma pack(pop)

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

#ifdef __ANDROID__
	class AutoCloseAnd
	{
		POFILE* in;
	public:
		AutoCloseAnd(POFILE* in)
			: in(in)
		{}

		~AutoCloseAnd() {
			and_pclose(in);
		}
	};
#endif
}

void Partclone::init()
{
	if (has_error)
		return;

	std::string cmd = "blkid -o export \""+dev->getFilename()+"\"";
	std::string blkid_out;
	if(os_popen(cmd, blkid_out)!=0)
	{
		has_error = true;
		return;
	}

	fstype = strlower(trim(getbetween("TYPE=", "\n", blkid_out)));

	Server->Log("Detected fs type "+fstype);

	if(fstype!="ext2" && fstype!="ext3"
		&& fstype!="ext4" && fstype!="xfs")
	{
		Server->Log("Fs type not supported");
		has_error = true;
		return;
	}

	cmd = "partclone."+fstype+" -o - -c -s \"" + dev->getFilename() + "\"";
	FILE* in = nullptr;
#ifdef __ANDROID__
	POFILE* pin = NULL;
#endif

#ifdef __ANDROID__
	pin = and_popen(cmd.c_str(), "r");
	if (pin != NULL) in = pin->fp;
#elif __linux__
	in = _popen(cmd.c_str(), "re");
	if (!in) in = _popen(cmd.c_str(), "r");
#else
	in = _popen(cmd.c_str(), "r");
#endif

	if (in == nullptr)
	{
		has_error = true;
		return;
	}

#ifdef __ANDROID__
	AutoCloseAnd close_in(pin);
#else
	AutoClose close_in(in);
#endif

	partclone_header header;
	if (!read_in(in, reinterpret_cast<char*>(&header), sizeof(header)))
	{
		has_error = true;
		return;
	}

	unsigned int curr_crc = mz_crc32(0, reinterpret_cast<unsigned char*>(&header), sizeof(header) - sizeof(unsigned int));
	curr_crc = ~curr_crc;

	if (curr_crc != header.crc)
	{
		has_error = true;
		return;
	}

	header.magic[15] = 0;

	if (std::string(header.magic) != "partclone-image")
	{
		has_error = true;
		return;
	}

	std::string image_version(header.image_version, 4);
	if (watoi(image_version) != 2)
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

	curr_crc = mz_crc32(mz_crc32(0, nullptr, 0), bitmap, bitmap_bytes);
	curr_crc = ~curr_crc;

	if (curr_crc != bitmap_crc)
	{
		has_error = true;
		return;
	}

	block_size = header.block_size;
	total_size = header.device_size;

	unsigned int* bitmap_test = reinterpret_cast<unsigned int*>(bitmap);
	const unsigned int bits_per = sizeof(unsigned int) * 8;

	for (unsigned int nr = 0; nr < total_size/ block_size; ++nr)
	{
		unsigned int offset = nr / bits_per;
		unsigned int bit = nr & (bits_per - 1);
		bool has_bit = (bitmap_test[offset] >> bit) & 1;

		assert(hasBlock(nr) == has_bit);
	}
}

std::string Partclone::getType()
{
	return fstype;
}
