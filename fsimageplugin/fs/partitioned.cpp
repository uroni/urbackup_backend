#include "partitioned.h"
#include <algorithm>
#include "../../stringtools.h"
#include "ntfs.h"
#include "../../Interface/Server.h"
#include <string.h>

namespace
{
	struct SPartition
	{
		int64 offset;
		int64 length;
	};

	std::string readAligned(IFile* dev, int64 offset, int64 length)
	{
		const int64 alignment = 4096;
		int64 offset_aligned = (offset / alignment)*alignment;
		int64 read_end = offset + length;
		int64 read_end_aligned = read_end;
		if (read_end_aligned%alignment != 0)
		{
			read_end_aligned = ((read_end / alignment)+1)*alignment;
		}

		std::string ret = dev->Read(offset_aligned, static_cast<_u32>(read_end_aligned - offset_aligned));

		if (ret.size() <= offset - offset_aligned)
		{
			return std::string();
		}
		else
		{
			size_t ret_start = offset - offset_aligned;
			size_t ret_end = (std::min)(ret.size(), static_cast<size_t>(offset - offset_aligned + length));
			return ret.substr(ret_start, ret_end - ret_start);
		}
	}

#pragma pack(push)
#pragma pack(1)
	struct MBRPartition
	{
		unsigned char boot_flag;
		unsigned char chs_begin[3];
		unsigned char sys_type;
		unsigned char chs_end[3];
		unsigned int start_sector;
		unsigned int nr_sector;
	};
#pragma pack(pop)

	std::vector<SPartition> readPartitionsMBR(IFile* dev, const std::string& mbr)
	{
		const MBRPartition* partitions = reinterpret_cast<const MBRPartition*>(&mbr[446]);
		const int64 c_sector_size = 512;

		std::vector<SPartition> ret;
		for (size_t i = 0; i < 4; ++i)
		{
			const MBRPartition* cpart = &partitions[i];
			if (cpart->sys_type == 0)
			{
				continue;
			}

			SPartition part;
			part.offset = cpart->start_sector*c_sector_size;
			part.length = cpart->nr_sector*c_sector_size;
			ret.push_back(part);
		}

		return ret;
	}

#pragma pack(push)
#pragma pack(1)
	struct EfiHeader
	{
		uint64 signature;
		_u32 revision;
		_u32 header_size;
		_u32 header_crc;
		_u32 reserved;
		int64 current_lba;
		int64 backup_lba;
		int64 first_lba;
		int64 last_lba;
		char disk_guid[16];
		int64 partition_table_lba;
		_u32 num_parition_entries;
		_u32 partition_entry_size;
		_u32 partition_table_crc;
	};

	struct GPTPartition
	{
		char partition_type_guid[16];
		char unique_partition_guid[16];
		int64 first_lba;
		int64 last_lba;
		uint64 flags;
		char name[72];
	};
#pragma pack(pop)

	std::vector<SPartition> readPartitionsGPT(IFile* dev, const std::string& gpt_header)
	{
		const EfiHeader* efi_header = reinterpret_cast<const EfiHeader*>(&gpt_header[0]);
		const int64 c_sector_size = 512;

		int64 table_pos = efi_header->partition_table_lba*c_sector_size;

		char partition_type_guid_empty[16] = {};

		std::vector<SPartition> ret;
		for (_u32 i = 0; i < efi_header->num_parition_entries; ++i)
		{
			std::string table_entry = readAligned(dev, table_pos, efi_header->partition_entry_size);
			if (table_entry.size() < sizeof(GPTPartition))
			{
				continue;
			}
			table_pos += efi_header->partition_entry_size;

			const GPTPartition* cpart = reinterpret_cast<const GPTPartition*>(table_entry.data());

			if (cpart->first_lba == 0
				&& cpart->last_lba == 0
				&& cpart->flags == 0
				&& memcmp(cpart->partition_type_guid, partition_type_guid_empty, sizeof(partition_type_guid_empty)) == 0)
			{
				continue;
			}

			SPartition new_part;
			new_part.offset = cpart->first_lba*c_sector_size;
			new_part.length = (cpart->last_lba - cpart->first_lba + 1)*c_sector_size;
			ret.push_back(new_part);
		}
		return ret;
	}

	std::vector<SPartition> readPartitions(IFile* dev)
	{
		std::string mbr = readAligned(dev, 0, 512);
		std::string gpt_header = readAligned(dev, 512, 512);

		if (next(gpt_header, 0, "EFI PART"))
		{
			return readPartitionsGPT(dev, gpt_header);
		}
		else if(mbr.size()>=512
			&& static_cast<unsigned char>(mbr[510]) == 0x55
			&& static_cast<unsigned char>(mbr[511]) == 0xAA)
		{
			return readPartitionsMBR(dev, mbr);
		}

		return std::vector<SPartition>();
	}

	bool isNTFS(IFile* dev)
	{
		std::string buffer = readAligned(dev, 0, 50);

		if (buffer.size() < 10)
		{
			return false;
		}

		if (buffer[3] == 'N' && buffer[4] == 'T' && buffer[5] == 'F' && buffer[6] == 'S')
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	class PartitionWrapper : public IFile
	{
	public:
		PartitionWrapper(IFile* wfile, SPartition part)
			: wfile(wfile), part(part), pos(0)
		{
			wfile->Seek(part.offset);
		}

		virtual std::string Read(_u32 tr, bool *has_error)
		{
			if (!check_pos(pos))
				return std::string();

			tr = adjust_tr(pos, tr);

			std::string ret = wfile->Read(tr, has_error);
			pos += ret.size();
			return ret;
		}

		virtual std::string Read(int64 spos, _u32 tr, bool *has_error)
		{
			if (!check_pos(spos))
				return std::string();

			tr = adjust_tr(spos, tr);

			return wfile->Read(spos + part.offset, tr, has_error);
		}

		virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error)
		{
			if (!check_pos(pos))
				return 0;

			bsize = adjust_tr(pos, bsize);

			_u32 r = wfile->Read(buffer, bsize, has_error);
			pos += r;
			return r;
		}

		virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error)
		{
			if (!check_pos(spos))
				return 0;

			bsize = adjust_tr(spos, bsize);
			return wfile->Read(spos + part.offset, buffer, bsize, has_error);
		}

		virtual _u32 Write(const std::string &tw, bool *has_error)
		{
			if (!check_pos(pos))
				return 0;

			_u32 towrite = adjust_tr(pos, static_cast<_u32>(tw.size()));

			if (towrite < tw.size())
			{
				_u32 rc = wfile->Write(tw.substr(0, towrite), has_error);
				pos += rc;
				return rc;
			}

			_u32 rc = wfile->Write(tw, has_error);
			pos += rc;
			return rc;
		}

		virtual _u32 Write(int64 spos, const std::string &tw, bool *has_error)
		{
			if (!check_pos(spos))
				return 0;

			_u32 towrite = adjust_tr(spos, static_cast<_u32>(tw.size()));

			if (towrite < tw.size())
			{
				_u32 rc = wfile->Write(spos + part.offset, tw.substr(0, towrite), has_error);
				pos += rc;
				return rc;
			}

			_u32 rc = wfile->Write(spos + part.offset, tw, has_error);
			pos += rc;
			return rc;
		}

		virtual _u32 Write(const char* buffer, _u32 bsize, bool *has_error)
		{
			if (!check_pos(pos))
				return 0;

			bsize = adjust_tr(pos, bsize);
			_u32 rc = wfile->Write(buffer, bsize, has_error);
			pos += rc;
			return rc;
		}

		virtual _u32 Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error)
		{
			if (!check_pos(spos))
				return 0;

			bsize = adjust_tr(spos, bsize);
			return wfile->Write(spos + part.offset, buffer, bsize, has_error);
		}

		virtual bool Seek(_i64 spos)
		{
			if (!check_pos(spos))
			{
				return false;
			}

			return wfile->Seek(spos + part.offset);
		}

		virtual _i64 Size(void)
		{
			return part.length;
		}

		virtual _i64 RealSize()
		{
			return part.length;
		}

		virtual std::string getFilename(void)
		{
			return wfile->getFilename() + ":" + convert(part.offset) + "-" + convert(part.offset + part.length);
		}

		virtual bool PunchHole(_i64 spos, _i64 size)
		{
			if (!check_pos(spos))
				return false;

			if (spos + size > part.length)
				return false;

			return wfile->PunchHole(spos + part.offset, size);
		}

		virtual bool Sync()
		{
			return wfile->Sync();
		}

	private:
		_u32 adjust_tr(int64 spos, _u32 tr)
		{
			if (spos + tr > part.length)
				return static_cast<_u32>(part.length - spos);
			return tr;
		}

		bool check_pos(int64 spos)
		{
			if (spos >= part.length
				|| spos<0)
				return false;

			return true;
		}

		int64 pos;
		SPartition part;
		IFile* wfile;
};
}


FSPartitioned::FSPartitioned(const std::string & pDev, 
	IFSImageFactory::EReadaheadMode read_ahead,
	bool background_priority,
	IFsNextBlockCallback * next_block_callback)
	: Filesystem(pDev, read_ahead, next_block_callback),
	bitmap(NULL)
{
	if (has_error)
	{
		return;
	}
	
#ifdef _WIN32
	IFsFile* fs_dev = dynamic_cast<IFsFile*>(dev);
	if (fs_dev == NULL)
	{
		Server->Log("Device is not a file system file in FSPartitioned() for device " + pDev, LL_ERROR);
		has_error = true;
		return;
	}

	DWORD r_bytes;
	GET_LENGTH_INFORMATION li;
	BOOL b = DeviceIoControl(fs_dev->getOsHandle(), IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &li, sizeof(GET_LENGTH_INFORMATION), &r_bytes, NULL);
	if (!b)
	{
		Server->Log("Error in DeviceIoControl(IOCTL_DISK_GET_LENGTH_INFO) in FSPartitioned() for device "+pDev, LL_ERROR);
		has_error = true;
		return;
	}
	drivesize = li.Length.QuadPart;
#else
	drivesize = dev->Size();
#endif

	std::vector<SPartition> partitions = readPartitions(dev);
	std::vector<IFilesystem*> filesystems;
	filesystems.resize(partitions.size());

	str_map params;

	blocksize = -1;
	for (size_t i = 0; i < partitions.size(); ++i)
	{
		PartitionWrapper partition_file(dev, partitions[i]);
		if (isNTFS(&partition_file))
		{
			filesystems[i] = new FSNTFS(&partition_file, IFSImageFactory::EReadaheadMode_None,
				false, NULL);

			if (filesystems[i]->hasError())
			{
				delete filesystems[i];
				filesystems[i] = NULL;
				continue;
			}

			if (blocksize == -1
				|| filesystems[i]->getBlocksize() < blocksize)
			{
				int64 fsblocksize = filesystems[i]->getBlocksize();
				if (blocksize != -1)
				{
					while (blocksize%fsblocksize != 0)
					{
						fsblocksize /= 2;
					}
				}

				blocksize = fsblocksize;
			}
		}
	}

	if (blocksize == -1)
	{
		blocksize = 4096;

		if (drivesize%blocksize != 0)
		{
			Server->Log("Drive size not aligned to 4096 bytes. Using 512 bytes.", LL_INFO);
			blocksize = 512;
		}
	}

	int64 bitmap_entries = (int64)(drivesize / blocksize);
	if (drivesize%blocksize != 0)
		++bitmap_entries;

	size_t bitmap_bytes = (size_t)(bitmap_entries / 8);

	if (bitmap_entries % 8 != 0)
		++bitmap_bytes;

	bitmap = new unsigned char[bitmap_bytes];
	memset(bitmap, 0xFF, bitmap_bytes);

	for (size_t i = 0; i < partitions.size(); ++i)
	{
		if (filesystems[i] != NULL)
		{
			int64 drive_block = partitions[i].offset / blocksize;
			int64 blocks_per_fsblock = filesystems[i]->getBlocksize() / blocksize;
			for (int64 block = 0,
				nblocks = filesystems[i]->getSize() / filesystems[i]->getBlocksize();
				block < nblocks;
				++block, drive_block+=blocks_per_fsblock)
			{
				bool has_bit = filesystems[i]->hasBlock(block);
				if (!has_bit)
				{
					for (int64 j = 0; j < blocks_per_fsblock; ++j)
					{
						setHasBlock(drive_block + j, false);
					}
				}
			}
		}
	}

	for (size_t i = 0; i < filesystems.size(); ++i)
	{
		delete filesystems[i];
	}

	initReadahead(read_ahead, background_priority);
}

FSPartitioned::~FSPartitioned(void)
{
	delete[] bitmap;
}

int64 FSPartitioned::getBlocksize(void)
{
	return blocksize;
}

int64 FSPartitioned::getSize(void)
{
	return drivesize;
}

const unsigned char * FSPartitioned::getBitmap(void)
{
	return bitmap;
}

void FSPartitioned::setHasBlock(int64 pBlock, bool v)
{
	int64 blocksize = getBlocksize();

	size_t bitmap_byte = (size_t)(pBlock / 8);
	size_t bitmap_bit = pBlock % 8;

	unsigned char& b = bitmap[bitmap_byte];

	if (v)
		b = b | (1 << bitmap_bit);
	else
		b = b&(~(1 << bitmap_bit));
}
