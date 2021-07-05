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

#include "vhdxfile.h"
#include "../stringtools.h"
#include <assert.h>
#include <cstring>
#include "CompressedFile.h"
#include "../urbackupcommon/os_functions.h"
#include "FileWrapper.h"
#include "ClientBitmap.h"
#include "IFilesystem.h"
#include "fs/ntfs.h"

#define PAYLOAD_BLOCK_NOT_PRESENT 0
#define PAYLOAD_BLOCK_UNDEFINED 1
#define PAYLOAD_BLOCK_ZERO 2
#define PAYLOAD_BLOCK_UNMAPPED 3
#define PAYLOAD_BLOCK_FULLY_PRESENT 6
#define PAYLOAD_BLOCK_PARTIALLY_PRESENT 7

namespace
{
	const int64 vhdx_header_length = 3 * 1024 * 1024;
	const int64 allocate_size_add_size = 100 * 1024 * 1024;
	const _u32 log_sector_size = 4096;

	template<typename T>
	auto roundUp(T numToRound, T multiple)
	{
		return ((numToRound + multiple - 1) / multiple) * multiple;
	}

	std::vector<char> getFileIdentifier()
	{
		std::vector<char> ret;
		ret.resize(500);
		std::memcpy(ret.data(), "vhdxfile", 8);
		std::string creator = Server->ConvertToUTF16("UrBackup vhdx file");
		std::memcpy(ret.data() + 8, creator.data(), creator.size());
		return ret;
	}

	void secureRandomGuid(VhdxGUID& g)
	{
		Server->secureRandomFill(g, 16);
		g[6] = 0x40 | (g[6] & 0xf);
		g[8] = 0x80 | (g[8] & 0x3f);
	}

	void randomGuid(VhdxGUID& g)
	{
		Server->randomFill(g, 16);
		g[6] = 0x40 | (g[6] & 0xf);
		g[8] = 0x80 | (g[8] & 0x3f);
	}

	void zeroGUID(VhdxGUID& g)
	{
		memset(g, 0, 16);
	}

	bool equalsGUID(const VhdxGUID& a, const VhdxGUID& b)
	{
		return memcmp(a, b, sizeof(VhdxGUID)) == 0;
	}

	bool isZeroGUID(VhdxGUID& g)
	{
		VhdxGUID z = {};
		return equalsGUID(g, z);
	}

	void copyGUID(const VhdxGUID& src, VhdxGUID& dst)
	{
		std::memcpy(dst, src, 16);
	}

	void reorderGUID(VhdxGUID& g)
	{
		*reinterpret_cast<unsigned int*>(&g[0]) = big_endian(*reinterpret_cast<unsigned int*>(&g[0]));
		*reinterpret_cast<unsigned short*>(&g[4]) = big_endian(*reinterpret_cast<unsigned short*>(&g[4]));
		*reinterpret_cast<unsigned short*>(&g[6]) = big_endian(*reinterpret_cast<unsigned short*>(&g[6]));
	}

	bool parseStrGuid(const std::string& str, VhdxGUID& g)
	{
		if (str.size() < 5)
			return false;

		if (str[0] != '{' || str[str.size() - 1] != '}')
			return false;

		std::string hb;
		for (size_t i = 1; i < str.size() - 1; ++i)
		{
			if(IsHex(str.substr(i, 1) ) )
				hb+=str[i];
		}

		if (hb.size() != 32)
			return false;

		for (size_t i = 0; i < hb.size(); i += 2)
		{
			std::string cb = hb.substr(i, 2);
			g[i/2] = static_cast<unsigned char>(hexToULong(cb));
		}

		reorderGUID(g);

		return true;
	}

	std::string strGUID(const VhdxGUID& g)
	{
		VhdxGUID tmp;
		copyGUID(g, tmp);
		reorderGUID(tmp);

		std::string ret = "{";

		for (size_t i = 0; i < 16; ++i)
		{
			ret += byteToHex(tmp[i]);

			if (i == 3 || i==5 || i==7 || i==9)
				ret += "-";
		}

		return ret + "}";
	}

	unsigned int crc32c(unsigned char* data, size_t data_size)
	{
		unsigned int crc = 0xFFFFFFFF;
		for (size_t i = 0; i < data_size; ++i)
		{
			unsigned int b = data[i];
			crc = crc ^ b;
			for (int j = 7; j >= 0; j--)
			{
				unsigned int mask = -1 * (crc & 1);
				crc = (crc >> 1) ^ (0x82F63B78 & mask);
			}
		}
		return ~crc;
	}

	std::vector<char> getVhdxHeader(uint64 SequenceNumber)
	{
		std::vector<char> ret;
		ret.resize(sizeof(VhdxHeader));
		VhdxHeader* vhdxHeader = reinterpret_cast<VhdxHeader*>(ret.data());
		std::memcpy(ret.data(), "head", 4);
		vhdxHeader->SequenceNumber = SequenceNumber;
		secureRandomGuid(vhdxHeader->FileWriteGuid);
		secureRandomGuid(vhdxHeader->DataWriteGuid);
		vhdxHeader->Version = 1;
		vhdxHeader->LogOffset = 1 * 1024 * 1024;
		vhdxHeader->LogLength = 1 * 1024 * 1024;
		vhdxHeader->Checksum = crc32c(reinterpret_cast<unsigned char*>(&ret[0]), ret.size());
		return ret;
	}

	bool checkHeader(IFile* backing_file, VhdxHeader& header)
	{
		std::string ident(reinterpret_cast<char*>(&header), 4);
		if (ident != "head")
		{
			Server->Log("VHDX header tag wrong", LL_WARNING);
			return false;
		}

		_u32 ccrc = header.Checksum;

		header.Checksum = 0;

		if (crc32c(reinterpret_cast<unsigned char*>(&header), sizeof(header)) != ccrc)
		{
			header.Checksum = ccrc;
			Server->Log("VHDX header checksum wrong", LL_WARNING);
			return false;
		}

		header.Checksum = ccrc;
		return true;
	}

#pragma pack(1)
	struct VhdxRegionTableHeader
	{
		_u32 Signature;
		_u32 Checksum;
		_u32 EntryCount;
		_u32 Reserved;
	};
#pragma pack()

	int64 getDataBlocks(int64 rawf_size, _u32 block_size)
	{
		int64 data_blocks = rawf_size / block_size;
		if (rawf_size % block_size != 0) ++data_blocks;
		return data_blocks;
	}

	_u32 getChunkRatio(_u32 block_size, _u32 sector_size)
	{
		return static_cast<_u32>((8388608LL * sector_size) / block_size);
	}

	_u32 getBatEntries(int64 size, _u32 block_size, _u32 sector_size)
	{
		int64 data_blocks = getDataBlocks(size, block_size);
		return static_cast<_u32>(data_blocks + (data_blocks - 1) / getChunkRatio(block_size, sector_size));
	}

	_u32 getBatEntry(int64 pos, _u32 block_size, _u32 sector_size)
	{
		int64 data_blocks = pos / block_size;
		return static_cast<_u32>(data_blocks + (data_blocks - 1) / getChunkRatio(block_size, sector_size));
	}

	_u32 getSectorBitmapEntry(int64 pos, _u32 block_size, _u32 sector_size)
	{
		int64 data_blocks = pos / block_size;
		_u32 chunk_ratio = getChunkRatio(block_size, sector_size);
		return static_cast<_u32>(data_blocks + (data_blocks - 1) / chunk_ratio +
			(chunk_ratio - data_blocks%chunk_ratio));
	}

	_u32 getSectorBitmapOffset(int64 pos, _u32 block_size, _u32 sector_size)
	{
		int64 sector = pos / sector_size;
		return static_cast<_u32>(sector % 8388608LL);
	}

	bool isSectorSetInt(const char* sector_buf, 
		int64 pos, _u32 block_size, _u32 sector_size)
	{
		_u32 offs = getSectorBitmapOffset(pos, block_size, sector_size);
		const char* byte = sector_buf + offs / 8;
		_u32 bitmap_bit = offs % 8;

		bool has_bit = (( (*byte) & (1 << bitmap_bit)) > 0);

		return has_bit;
	}

	void setSectorInt(char* sector_buf,
		int64 start, int64 end, _u32 block_size, _u32 sector_size)
	{
		while (start < end)
		{
			_u32 offs = getSectorBitmapOffset(start, block_size, sector_size);
			char* byte = sector_buf + offs / 8;
			_u32 bitmap_bit = offs % 8;

			*byte = *byte | (1 << bitmap_bit);
			start += sector_size;
		}
	}

	_u32 getBatLength(int64 rawf_size, _u32 block_size, _u32 sector_size)
	{
		int64 bat_entries = getBatEntries(rawf_size, block_size, sector_size);

		int64 mb_blocks = (bat_entries * sizeof(uint64)) / block_size;
		if (bat_entries % block_size != 0) ++mb_blocks;

		return static_cast<_u32>(mb_blocks * 1024 * 1024);
	}

	_u32 getSectorBitmapBlocksLength(int64 rawf_size, _u32 block_size, _u32 sector_size)
	{
		int64 data_blocks = getDataBlocks(rawf_size, block_size);
		_u32 chunk_ratio = getChunkRatio(block_size, sector_size);
		int64 sector_bitmaps = data_blocks / chunk_ratio;
		if (data_blocks % chunk_ratio != 0)++sector_bitmaps;

		return static_cast<_u32>(sector_bitmaps * 1 * 1024 * 1024);
	}

	int64 getMetadataSizeSize(int64 rawf_size, _u32 block_size, _u32 sector_size)
	{
		int64 data_blocks = getDataBlocks(rawf_size, block_size);
		_u32 chunk_ratio = getChunkRatio(block_size, sector_size);
		int64 sector_bitmaps = data_blocks / chunk_ratio;
		if (data_blocks % chunk_ratio != 0)++sector_bitmaps;

		// | -- HEADER -- | -- DATA BLOCKS -- | -- BAT -- | -- SECTOR BITMAP BLOCKS -- |

		return vhdx_header_length + data_blocks * block_size 
			+ getBatLength(rawf_size, block_size, sector_size) + getSectorBitmapBlocksLength(rawf_size, block_size, sector_size);
	}

	void makeMetaTableGUID(VhdxGUID& g)
	{
		unsigned char meta_guid[16] = { 0x8B, 0x7C, 0xA2, 0x06, 0x47, 0x90, 0x4B, 0x9A, 0xB8, 0xFE, 0x57, 0x5F, 0x05, 0x0F, 0x88, 0x6E };
		std::memcpy(g, meta_guid, sizeof(meta_guid));
		reorderGUID(g);
	}

	void makeBatGUID(VhdxGUID& g)
	{
		unsigned char bat_guid[16] = { 0x2D, 0xC2, 0x77, 0x66, 0xF6, 0x23, 0x42, 0x00, 0x9D, 0x64, 0x11, 0x5E, 0x9B, 0xFD, 0x4A, 0x08 };
		std::memcpy(g, bat_guid, sizeof(bat_guid));
		reorderGUID(g);
	}

	const uint64 meta_region_offset = 2 * 1024 * 1024;
	const uint64 bat_table_offset = meta_region_offset + 1 * 1024 * 1024;

	std::vector<char> getVhdxRegionTable(int64 rawf_size, _u32 block_size, _u32 sector_size)
	{
		std::vector<char> ret;
		ret.resize(64 * 1024);

		std::memcpy(ret.data(), "regi", 4);
		VhdxRegionTableHeader* header = reinterpret_cast<VhdxRegionTableHeader*>(ret.data());
		header->EntryCount = 2;

		VhdxRegionTableEntry* meta_entry = reinterpret_cast<VhdxRegionTableEntry*>(ret.data() + sizeof(VhdxRegionTableHeader));
		makeMetaTableGUID(meta_entry->Guid);
		meta_entry->FileOffset = meta_region_offset;
		meta_entry->Length = 1 * 1024 * 1024;
		meta_entry->Required = 1;

		VhdxRegionTableEntry* bat_entry = reinterpret_cast<VhdxRegionTableEntry*>(ret.data() + sizeof(VhdxRegionTableHeader) 
			+ sizeof(VhdxRegionTableEntry));
		makeBatGUID(bat_entry->Guid);
		bat_entry->FileOffset = bat_table_offset;
		bat_entry->Length = getBatLength(rawf_size, block_size, sector_size);
		bat_entry->Required = 1;

		header->Checksum = crc32c(reinterpret_cast<unsigned char*>(&ret[0]), ret.size());

		return ret;
	}

#pragma pack(1)
	struct VhdxMetadataTableHeader
	{
		uint64 Signature;
		unsigned short Reserved;
		unsigned short EntryCount;
		_u32 Reserved2[5];
	};

	struct VhdxMetadataTableEntry
	{
		VhdxGUID ItemId;
		_u32 Offset;
		_u32 Length;
		_u32 IsUser : 1;
		_u32 IsVirtualDisk : 1;
		_u32 IsRequired : 1;
		_u32 Reserved : 29;
		_u32 Reserved2;
	};

	struct VhdxVirtualDiskSize
	{
		uint64 VirtualDiskSize;
	};

	struct VhdxVirtualDiskLogicalSectorSize
	{
		_u32 LogicalSectorSize;
	};

	struct VhdxPhysicalDiskSectorSize
	{
		_u32 PhysicalSectorSize;
	};

	struct VhdxVirtualDiskId
	{
		VhdxGUID VirtualDiskId;
	};

	struct VhdxParentLocatorHeader
	{
		VhdxGUID LocatorType;
		unsigned short Reserved;
		unsigned short KeyValueCount;
	};

	struct VhdxParentLocatorEntry
	{
		_u32 KeyOffset;
		_u32 ValueOffset;
		unsigned short KeyLength;
		unsigned short ValueLength;
	};

#pragma pack()

	void makeFileParametersGUID(VhdxGUID& g)
	{
		unsigned char file_parameters_guid[16] = { 0xCA, 0xA1, 0x67, 0x37, 0xFA, 0x36, 0x4D, 0x43, 0xB3, 0xB6, 0x33, 0xF0, 0xAA, 0x44, 0xE7, 0x6B };
		std::memcpy(&g, file_parameters_guid, sizeof(file_parameters_guid));
		reorderGUID(g);
	}

	void makeVirtualDiskSizeGUID(VhdxGUID& g)
	{
		unsigned char virtual_disk_size_guid[16] = { 0x2F, 0xA5, 0x42, 0x24, 0xCD, 0x1B, 0x48, 0x76, 0xB2, 0x11, 0x5D, 0xBE, 0xD8, 0x3B, 0xF4, 0xB8 };
		std::memcpy(&g, virtual_disk_size_guid, sizeof(virtual_disk_size_guid));
		reorderGUID(g);
	}

	void makeLogicalSectorSizeGUID(VhdxGUID& g)
	{
		unsigned char logical_sector_size_guid[16] = { 0x81, 0x41, 0xBF, 0x1D, 0xA9, 0x6F, 0x47, 0x09, 0xBA, 0x47, 0xF2, 0x33, 0xA8, 0xFA, 0xAB, 0x5F };
		std::memcpy(&g, logical_sector_size_guid, sizeof(logical_sector_size_guid));
		reorderGUID(g);
	}

	void makePhysicalSectorSizeGUID(VhdxGUID& g)
	{
		unsigned char physical_sector_size_guid[16] = { 0xCD, 0xA3, 0x48, 0xC7, 0x44, 0x5D, 0x44, 0x71, 0x9C, 0xC9, 0xE9, 0x88, 0x52, 0x51, 0xC5, 0x56 };
		std::memcpy(&g, physical_sector_size_guid, sizeof(physical_sector_size_guid));
		reorderGUID(g);
	}

	void makeVirtualDiskIdGUID(VhdxGUID& g)
	{
		unsigned char page83_data_guid[16] = { 0xBE, 0xCA, 0x12, 0xAB, 0xB2, 0xE6, 0x45, 0x23, 0x93, 0xEF, 0xC3, 0x09, 0xE0, 0x00, 0xC7, 0x46 };
		std::memcpy(&g, page83_data_guid, sizeof(page83_data_guid));
		reorderGUID(g);
	}

	void makeParentLocatorGUID(VhdxGUID& g)
	{
		unsigned char parent_locator_guid[16] = { 0xA8, 0xD3, 0x5F, 0x2D, 0xB3, 0x0B, 0x45, 0x4D, 0xAB, 0xF7, 0xD3, 0xD8, 0x48, 0x34, 0xAB, 0x0C };
		std::memcpy(&g, parent_locator_guid, sizeof(parent_locator_guid));
		reorderGUID(g);
	}

	void makeVhdxParentLocatorGUID(VhdxGUID& g)
	{
		unsigned char vhdx_parent_locator_guid[16] = { 0xB0, 0x4A, 0xEF, 0xB7, 0xD1, 0x9E, 0x4A, 0x81, 0xB7, 0x89, 0x25,
			0xB8, 0xE9, 0x44, 0x59, 0x13 };
		std::memcpy(&g, vhdx_parent_locator_guid, sizeof(vhdx_parent_locator_guid));
		reorderGUID(g);
	}

	std::vector<char> getMetaRegion(int64 rawf_size, _u32 block_size, _u32 sector_size,
		std::string parent_data_uuid, std::string parent_rel_loc, std::string parent_abs_loc)
	{
		size_t parent_locator_size = 0;

		str_map parent_loc_entries;

		if (!parent_data_uuid.empty())
		{
			parent_loc_entries[Server->ConvertToUTF16("parent_linkage")] = Server->ConvertToUTF16(parent_data_uuid);
			parent_loc_entries[Server->ConvertToUTF16("relative_path")] = Server->ConvertToUTF16(parent_rel_loc);
			parent_loc_entries[Server->ConvertToUTF16("absolute_win32_path")] = Server->ConvertToUTF16(parent_abs_loc);

			parent_locator_size = sizeof(VhdxParentLocatorHeader);
			parent_locator_size += sizeof(VhdxParentLocatorEntry) * parent_loc_entries.size();

			for (auto it : parent_loc_entries)
			{
				parent_locator_size += it.first.size();
				parent_locator_size += it.second.size();
			}
		}

		std::vector<char> ret;
		ret.resize(64 * 1024
			+ sizeof(VhdxFileParameters)
			+ sizeof(VhdxVirtualDiskSize)
			+ sizeof(VhdxVirtualDiskLogicalSectorSize)
			+ sizeof(VhdxPhysicalDiskSectorSize)
			+ sizeof(VhdxVirtualDiskId)
			+ parent_locator_size);

		std::memcpy(ret.data(), "metadata", 8);
		VhdxMetadataTableHeader* header = reinterpret_cast<VhdxMetadataTableHeader*>(ret.data());
		header->EntryCount = 5;

		VhdxMetadataTableEntry* file_parameters_entry = reinterpret_cast<VhdxMetadataTableEntry*>(ret.data() + sizeof(VhdxMetadataTableHeader));
		makeFileParametersGUID(file_parameters_entry->ItemId);
		file_parameters_entry->Offset = 64 * 1024;
		file_parameters_entry->Length = sizeof(VhdxFileParameters);
		file_parameters_entry->IsRequired = 1;

		VhdxFileParameters* file_parameters = reinterpret_cast<VhdxFileParameters*>(ret.data() + file_parameters_entry->Offset);
		file_parameters->BlockSize = block_size;
		file_parameters->LeaveBlocksAllocated = 0;
		file_parameters->HasParent = parent_data_uuid.empty() ? 0 : 1;

		VhdxMetadataTableEntry* virtual_disk_size_entry = reinterpret_cast<VhdxMetadataTableEntry*>(ret.data() + sizeof(VhdxMetadataTableHeader) +
			sizeof(VhdxMetadataTableEntry));
		makeVirtualDiskSizeGUID(virtual_disk_size_entry->ItemId);
		virtual_disk_size_entry->Offset = 64 * 1024 + sizeof(VhdxFileParameters);
		virtual_disk_size_entry->Length = sizeof(VhdxFileParameters);
		virtual_disk_size_entry->IsRequired = 1;
		virtual_disk_size_entry->IsVirtualDisk = 1;

		VhdxVirtualDiskSize* virtual_disk_size = reinterpret_cast<VhdxVirtualDiskSize*>(ret.data() + virtual_disk_size_entry->Offset);
		virtual_disk_size->VirtualDiskSize = rawf_size;

		VhdxMetadataTableEntry* logical_sector_size_entry = reinterpret_cast<VhdxMetadataTableEntry*>(ret.data() + sizeof(VhdxMetadataTableHeader) +
			2 * sizeof(VhdxMetadataTableEntry));
		makeLogicalSectorSizeGUID(logical_sector_size_entry->ItemId);
		logical_sector_size_entry->Offset = 64 * 1024 + sizeof(VhdxFileParameters) + sizeof(VhdxVirtualDiskSize);
		logical_sector_size_entry->Length = sizeof(VhdxVirtualDiskLogicalSectorSize);
		logical_sector_size_entry->IsRequired = 1;
		logical_sector_size_entry->IsVirtualDisk = 1;

		VhdxVirtualDiskLogicalSectorSize* logical_sector_size = reinterpret_cast<VhdxVirtualDiskLogicalSectorSize*>(ret.data() + logical_sector_size_entry->Offset);
		logical_sector_size->LogicalSectorSize = sector_size;

		VhdxMetadataTableEntry* physical_sector_size_entry = reinterpret_cast<VhdxMetadataTableEntry*>(ret.data() + sizeof(VhdxMetadataTableHeader) +
			3 * sizeof(VhdxMetadataTableEntry));
		makePhysicalSectorSizeGUID(physical_sector_size_entry->ItemId);
		physical_sector_size_entry->Offset = 64 * 1024 + sizeof(VhdxFileParameters) + sizeof(VhdxVirtualDiskSize) + sizeof(VhdxVirtualDiskLogicalSectorSize);
		physical_sector_size_entry->Length = sizeof(VhdxPhysicalDiskSectorSize);
		physical_sector_size_entry->IsRequired = 1;
		physical_sector_size_entry->IsVirtualDisk = 1;

		VhdxPhysicalDiskSectorSize* physical_sector_size = reinterpret_cast<VhdxPhysicalDiskSectorSize*>(ret.data() + physical_sector_size_entry->Offset);
		physical_sector_size->PhysicalSectorSize = sector_size;

		VhdxMetadataTableEntry* page83_data_entry = reinterpret_cast<VhdxMetadataTableEntry*>(ret.data() + sizeof(VhdxMetadataTableHeader) +
			4 * sizeof(VhdxMetadataTableEntry));
		makeVirtualDiskIdGUID(page83_data_entry->ItemId);
		page83_data_entry->Offset = 64 * 1024 + sizeof(VhdxFileParameters) + sizeof(VhdxVirtualDiskSize) + sizeof(VhdxVirtualDiskLogicalSectorSize) + sizeof(VhdxPhysicalDiskSectorSize);
		page83_data_entry->Length = sizeof(VhdxVirtualDiskId);
		page83_data_entry->IsRequired = 1;
		page83_data_entry->IsVirtualDisk = 1;

		VhdxVirtualDiskId* virtual_disk_id = reinterpret_cast<VhdxVirtualDiskId*>(ret.data() + page83_data_entry->Offset);
		secureRandomGuid(virtual_disk_id->VirtualDiskId);

		if (!parent_data_uuid.empty())
		{
			++header->EntryCount;

			VhdxMetadataTableEntry* parent_locator_entry = reinterpret_cast<VhdxMetadataTableEntry*>(ret.data() + sizeof(VhdxMetadataTableHeader) +
				5 * sizeof(VhdxMetadataTableEntry));
			makeParentLocatorGUID(parent_locator_entry->ItemId);
			parent_locator_entry->Offset = 64 * 1024 + sizeof(VhdxFileParameters) + sizeof(VhdxVirtualDiskSize)
				+ sizeof(VhdxVirtualDiskLogicalSectorSize) + sizeof(VhdxPhysicalDiskSectorSize) + sizeof(VhdxVirtualDiskId);
			parent_locator_entry->Length = static_cast<_u32>(parent_locator_size);
			parent_locator_entry->IsRequired = 1;

			VhdxParentLocatorHeader* parent_locator_header = reinterpret_cast<VhdxParentLocatorHeader*>(ret.data() + parent_locator_entry->Offset);

			parent_locator_header->KeyValueCount = static_cast<_u16>(parent_loc_entries.size());
			makeVhdxParentLocatorGUID(parent_locator_header->LocatorType);

			size_t entry_pos = parent_locator_entry->Offset + sizeof(VhdxParentLocatorHeader);
			size_t str_pos = parent_locator_entry->Offset + sizeof(VhdxParentLocatorHeader) +
				sizeof(VhdxParentLocatorEntry) * parent_loc_entries.size();

			for (auto it: parent_loc_entries)
			{
				VhdxParentLocatorEntry* entry = reinterpret_cast<VhdxParentLocatorEntry*>(ret.data() + entry_pos);
				entry_pos += sizeof(VhdxParentLocatorEntry);

				entry->KeyOffset = static_cast<_u32>(str_pos - parent_locator_entry->Offset);
				entry->KeyLength = static_cast<_u16>(it.first.size());
				std::memcpy(ret.data() + str_pos, it.first.data(), it.first.size());
				str_pos += it.first.size();

				entry->ValueOffset = static_cast<_u32>(str_pos - parent_locator_entry->Offset);
				entry->ValueLength = static_cast<_u16>(it.second.size());
				std::memcpy(ret.data() + str_pos, it.second.data(), it.second.size());
				str_pos += it.second.size();
			}

			assert(str_pos == ret.size());
		}

		return ret;
	}

#pragma pack(1)
	struct LogEntryHeader
	{
		_u32 signature; 
		_u32 Checksum; 
		_u32 EntryLength;
		_u32 Tail;
		int64 SequenceNumber;
		_u32 DescriptorCount;
		_u32 Reserved;
		VhdxGUID LogGuid;
		int64 FlushedFileOffset;
		int64 LastFileOffset;
	};

	struct LogZeroDescriptor
	{
		_u32 signature;
		_u32 Reserved;
		int64 ZeroLength;
		int64 FileOffset;
		int64 SequenceNumber;
	};

	struct LogDataDescriptor
	{
		_u32 signature;
		char TrailingBytes[4];
		char LeadingBytes[8];
		int64 FileOffset;
		int64 SequenceNumber;
	};

	struct LogDataSector
	{
		_u32 signature;
		_u32 SequenceHigh;
		char data[4084];
		_u32 SequenceLow;
	};
#pragma pack()


	struct LogData
	{
		int64 offset;
		char data[4096];
	};

	struct LogEntry
	{
		std::vector<LogZeroDescriptor> to_zero;
		std::vector<LogData> to_write;
		int64 sequence_number = -1;
		int64 length;
		int64 fsize;
		int64 new_fsize;
		uint64 tail_pos;
	};

	typedef union
	{
		struct {
			_u32 LowPart;
			_u32 HighPart;
		};
		struct {
			_u32 LowPart;
			_u32 HighPart;
		} u;
		int64 QuadPart;
	} SSequence;

	LogEntry readLogEntry(IFile* f, const VhdxGUID& log_guid, int64 off)
	{
		LogEntry loge;

		std::vector<char> buf(4096);

		if (f->Read(off, buf.data(), static_cast<_u32>(buf.size())) != buf.size())
		{
			Server->Log("Error reading log entry header. " + os_last_error_str(), LL_WARNING);
			return loge;
		}

		std::string signature(buf.data(), 4);

		if (signature != "loge")
			return loge;

		LogEntryHeader* header = reinterpret_cast<LogEntryHeader*>(buf.data());

		if (!equalsGUID(header->LogGuid, log_guid))
			return loge;

		loge.length = header->EntryLength;
		std::vector<char> entry_buf(header->EntryLength);

		if (f->Read(off, entry_buf.data(), static_cast<_u32>(entry_buf.size())) != entry_buf.size())
		{
			Server->Log("Error reading log entry (size=" + std::to_string(header->EntryLength) + "). "
				+ os_last_error_str(), LL_WARNING);
			return loge;
		}

		_u32 checksum = header->Checksum;

		header = reinterpret_cast<LogEntryHeader*>(entry_buf.data());
		header->Checksum = 0;

		_u32 checksum_calc = crc32c(reinterpret_cast<unsigned char*>(entry_buf.data()), entry_buf.size());

		if (checksum_calc != checksum)
		{
			Server->Log("Log entry checksum is wrong", LL_WARNING);
			return loge;
		}

		int64 entry_seq = header->SequenceNumber;

		loge.fsize = header->FlushedFileOffset;
		loge.new_fsize = header->LastFileOffset;
		loge.tail_pos = header->Tail;

		int64 desc_off = 4096;
		if (header->DescriptorCount > 126)
		{
			desc_off += ((header->DescriptorCount - 126) / 128 ) *4096;
			if ( (header->DescriptorCount - 126) % 128 != 0)
				desc_off += 4096;
		}

		for (int64 i = 0; i < header->DescriptorCount; ++i)
		{
			char* desc_ptr = entry_buf.data() + 64 + i * 32;
			std::string desc_sig(desc_ptr, 4);

			if (desc_sig == "zero")
			{
				LogZeroDescriptor* zero_desc = reinterpret_cast<LogZeroDescriptor*>(desc_ptr);
				if (entry_seq != zero_desc->SequenceNumber)
				{
					Server->Log("Zero log entry sequence number is wrong", LL_WARNING);
					return loge;
				}

				loge.to_zero.push_back(*zero_desc);
			}
			else if (desc_sig == "desc")
			{
				LogDataDescriptor* data_desc = reinterpret_cast<LogDataDescriptor*>(desc_ptr);
				if (entry_seq != data_desc->SequenceNumber)
				{
					Server->Log("Data log entry sequence number is wrong", LL_WARNING);
					return loge;
				}

				LogDataSector* data_sec = reinterpret_cast<LogDataSector*>(entry_buf.data() + desc_off);

				std::string data_sec_sig(entry_buf.data() + desc_off, 4);

				if (data_sec_sig != "data")
				{
					Server->Log("Data log entry signature is wrong", LL_WARNING);
					return loge;
				}

				SSequence seq;
				seq.QuadPart = data_desc->SequenceNumber;

				if (data_sec->SequenceHigh != seq.HighPart)
				{
					Server->Log("Data log entry high sequence number is wrong", LL_WARNING);
					return loge;
				}

				if (data_sec->SequenceLow != seq.LowPart)
				{
					Server->Log("Data log entry low sequence number is wrong", LL_WARNING);
					return loge;
				}

				LogData log_data;
				log_data.offset = data_desc->FileOffset;
				std::memcpy(log_data.data, data_desc->LeadingBytes, 8);
				std::memcpy(log_data.data + 8, data_sec->data, sizeof(data_sec->data));
				std::memcpy(log_data.data + 8 + sizeof(data_sec->data), data_desc->TrailingBytes, 4);

				loge.to_write.push_back(log_data);
			}
			else
			{
				Server->Log("Unknown log entry signature", LL_WARNING);
				return loge;
			}
		}

		loge.sequence_number = entry_seq;
		return loge;
	}
}

VHDXFile::VHDXFile(const std::string& fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize, bool fast_mode, bool compress, size_t compress_n_threads)
	: dst_size(pDstsize), fast_mode(fast_mode), read_only(pRead_only)
{
	is_open = open(fn, compress, compress_n_threads);
}

VHDXFile::VHDXFile(const std::string& fn, const std::string& parent_fn, bool pRead_only,
	bool fast_mode, bool compress, uint64 pDstsize, size_t compress_n_threads)
	: fast_mode(fast_mode), read_only(pRead_only), dst_size(pDstsize),
	parent_fn(parent_fn)
{
	if (!FileExists(fn))
	{
		parent = std::make_unique<VHDXFile>(parent_fn, true,
			0);

		if (!parent->isOpen())
		{
			Server->Log("Error opening VHDX parent at \"" + parent_fn + "\"", LL_ERROR);
			return;
		}

		dst_size = parent->getSize();

		if (pDstsize > 0 && pDstsize != dst_size)
		{
			dst_size = pDstsize;
		}
	}

	is_open = open(fn, compress, compress_n_threads);
}

VHDXFile::~VHDXFile()
{
	if (!is_open)
		return;

	if (!read_only)
	{
		finish();
	}
}

bool VHDXFile::Seek(_i64 offset)
{
	spos = offset;
	return true;
}

bool VHDXFile::Read(char* buffer, size_t bsize, size_t& read)
{
	bool has_read_error = false;
	read = Read(spos, buffer, static_cast<_u32>(bsize), &has_read_error);
	spos += read;
	return !has_read_error;
}

_u32 VHDXFile::Write(const char* buffer, _u32 bsize, bool* has_error)
{
	_u32 rc = Write(spos, buffer, bsize, has_error);
	spos += rc;
	return rc;
}

bool VHDXFile::isOpen(void)
{
	return is_open;
}

uint64 VHDXFile::getSize(void)
{
	return Size();
}

uint64 VHDXFile::usedSize()
{
	uint64 ret = 0;
	for (int64 i = 0; i < dst_size; i += block_size)
	{
		_u32 block = getBatEntry(spos, block_size, sector_size);

		VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + block;

		if (bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT ||
			bat_entry->State == PAYLOAD_BLOCK_FULLY_PRESENT)
			ret += block_size;
	}

	return ret;
}

std::string VHDXFile::getFilename(void)
{
	return file->getFilename();
}

bool VHDXFile::has_sector(_i64 sector_size)
{
	if (!has_sector_int(spos))
	{
		if (parent.get() != nullptr)
			return parent->has_sector_int(spos);
	}
	
	return true;
}

bool VHDXFile::this_has_sector(_i64 sector_size)
{
	return has_sector_int(spos);
}

unsigned int VHDXFile::getBlocksize()
{
	return block_size;
}

bool VHDXFile::finish()
{
	if (!finished)
	{
		finished = true;

		if (read_only)
			return true;

		bool ret = syncInt(true);

		if (ret && parent.get()!=nullptr)
		{
			ret = parent->finish();
		}

		CompressedFile* compfile = dynamic_cast<CompressedFile*>(file);
		if (compfile != nullptr)
		{
			if (compfile->finish())
			{
				finished = true;
				return true;
			}
		}
	}
	return true;
}

bool VHDXFile::trimUnused(_i64 fs_offset, _i64 trim_blocksize, ITrimCallback* trim_callback)
{
	return true;
}

bool VHDXFile::syncBitmap(_i64 fs_offset)
{
	return true;
}

bool VHDXFile::makeFull(_i64 fs_offset, IVHDWriteCallback* write_callback)
{
	FileWrapper devfile(this, fs_offset);
	std::unique_ptr<IReadOnlyBitmap> bitmap_source;

	bitmap_source.reset(new ClientBitmap(backing_file->getFilename() + ".cbitmap"));

	if (bitmap_source->hasError())
	{
		Server->Log("Error reading client bitmap. Falling back to reading bitmap from NTFS", LL_WARNING);

		bitmap_source.reset(new FSNTFS(&devfile, IFSImageFactory::EReadaheadMode_None, false, NULL));
	}

	if (bitmap_source->hasError())
	{
		Server->Log("Error opening NTFS bitmap. Cannot convert incremental to full image.", LL_WARNING);
		return false;
	}

	unsigned int bitmap_blocksize = static_cast<unsigned int>(bitmap_source->getBlocksize());

	std::vector<char> buffer;
	buffer.resize(sector_size);

	int64 ntfs_blocks_per_vhd_sector = block_size / bitmap_blocksize;

	for (int64 ntfs_block = 0, n_ntfs_blocks = devfile.Size() / bitmap_blocksize;
		ntfs_block < n_ntfs_blocks; ntfs_block += ntfs_blocks_per_vhd_sector)
	{
		bool has_vhd_sector = false;
		for (int64 i = ntfs_block;
			i < ntfs_block + ntfs_blocks_per_vhd_sector
			&& i < n_ntfs_blocks; ++i)
		{
			if (bitmap_source->hasBlock(i))
			{
				has_vhd_sector = true;
				break;
			}
		}

		if (has_vhd_sector)
		{
			int64 block_pos = fs_offset + ntfs_block * bitmap_blocksize;
			int64 max_block_pos = (std::min)(fs_offset + ntfs_block * bitmap_blocksize + block_size,
				fs_offset + n_ntfs_blocks * bitmap_blocksize);
			for (int64 i = block_pos; i < max_block_pos; i += sector_size)
			{
				Seek(i);

				if (!has_block(false)
					&& has_block(true))
				{
					bool has_error = false;
					if (Read(buffer.data(), sector_size) != sector_size)
					{
						Server->Log("Error converting incremental to full image. Cannot read from parent VHDX file at position " + convert(i), LL_WARNING);
						return false;
					}

					if (!write_callback->writeVHD(i, buffer.data(), sector_size))
					{
						Server->Log("Error converting incremental to full image. Cannot write to VHDX file at position " + convert(i), LL_WARNING);
						return false;
					}
				}
			}
		}
		else
		{
			int64 block_pos = ntfs_block * bitmap_blocksize;
			int64 max_block_pos = (std::min)(ntfs_block * bitmap_blocksize + block_size,
				n_ntfs_blocks * bitmap_blocksize);

			write_callback->emptyVHDBlock(block_pos, max_block_pos);
		}
	}

	parent.reset();
	parent_fn.clear();

	std::vector<char> meta_region = getMetaRegion(dst_size, block_size, sector_size,
		std::string(), std::string(), std::string());

	if (file->Write(meta_region_offset, meta_region.data(), static_cast<_u32>(meta_region.size())) != meta_region.size())
		return false;

	return true;
}

bool VHDXFile::setUnused(_i64 unused_start, _i64 unused_end)
{
	if (!Seek(unused_start))
	{
		Server->Log("Error while sseking to " + convert(unused_end) + " in VHDX file."
			"Size is " + convert(dst_size) + " -2", LL_ERROR);
		return false;
	}

	if (read_only)
	{
		Server->Log("VHDX file is read only -2", LL_ERROR);
		return false;
	}

	if (unused_end > dst_size)
	{
		Server->Log("VHDX file is not large enough. Want to trim till " + 
			convert(unused_end) + " but size is " + convert(dst_size), LL_ERROR);
		return false;
	}

	std::vector<char> zero_buf;

	while (unused_start< unused_end)
	{
		int64 block = getBatEntry(unused_start, block_size, sector_size);
		VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + block;

		if (unused_start % block_size == 0 &&
			unused_start + block_size <= unused_end)
		{
			bat_entry->State = PAYLOAD_BLOCK_ZERO;
			unused_start += block_size;
			continue;
		}

		_u32 curr_sector_size = sector_size;
		if (unused_start % sector_size != 0)
		{
			curr_sector_size = sector_size - unused_start % sector_size;
		}

		size_t wantwrite = (std::min)(static_cast<size_t>(curr_sector_size),
			static_cast<size_t>(unused_end - unused_start));

		bool copy_prev = false;

		if (bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT)
		{
			bool set;
			if (!isSectorSet(unused_start, set))
			{
				return false;
			}

			if (!set)
			{
				if (!setSector(unused_start))
				{
					return false;
				}

				copy_prev = true;
			}
			else
			{
				if (zero_buf.size() != wantwrite)
				{
					zero_buf.resize(wantwrite);
				}
				_u32 rc = file->Write(bat_entry->FileOffsetMB * 1024 * 1024 + unused_start % block_size,
					zero_buf.data(), static_cast<_u32>(wantwrite));
			}

			unused_start += wantwrite;
		}
		else if (bat_entry->State == PAYLOAD_BLOCK_UNDEFINED ||
			bat_entry->State == PAYLOAD_BLOCK_UNMAPPED ||
			bat_entry->State == PAYLOAD_BLOCK_NOT_PRESENT)
		{
			if (!allocateBatBlockFull(block))
			{
				return false;
			}

			bat_entry->State = PAYLOAD_BLOCK_PARTIALLY_PRESENT;

			if (!setSector(unused_start))
			{
				return false;
			}

			unused_start += wantwrite;

			copy_prev = true;
		}
		else if (bat_entry->State != PAYLOAD_BLOCK_ZERO)
		{
			Server->Log("Unknown bat entry state " + std::to_string(bat_entry->State), LL_ERROR);
			return false;
		}

		if (copy_prev && curr_sector_size < sector_size &&
			parent.get()!=nullptr)
		{
			std::vector<char> prev_buf(sector_size - curr_sector_size);

			int64 prev_pos = (unused_start / sector_size) * sector_size;

			_u32 rc = parent->Read(prev_pos,
				prev_buf.data(), static_cast<_u32>(prev_buf.size()));

			if (rc != prev_buf.size())
				return false;

			rc = file->Write(bat_entry->FileOffsetMB * 1024 * 1024 + prev_pos % block_size,
				prev_buf.data(), static_cast<_u32>(prev_buf.size()));

			if (rc != prev_buf.size())
				return false;
		}
		if (copy_prev && unused_start + wantwrite == unused_end &&
			unused_end % sector_size != 0)
		{
			std::vector<char> prev_buf(sector_size - unused_end%sector_size);

			int64 prev_pos = unused_end;

			_u32 rc = parent->Read(prev_pos,
				prev_buf.data(), static_cast<_u32>(prev_buf.size()));

			if (rc != prev_buf.size())
				return false;

			rc = file->Write(bat_entry->FileOffsetMB * 1024 * 1024 + prev_pos % block_size,
				prev_buf.data(), static_cast<_u32>(prev_buf.size()));

			if (rc != prev_buf.size())
				return false;
		}
	}

	return true;
}

bool VHDXFile::setBackingFileSize(_i64 fsize)
{
	if (file != backing_file.get())
	{
		return false;
	}

	fsize += 1 * 1024 * 1024;
	fsize += bat_region.Length;
	fsize += curr_header.LogLength;
	fsize += meta_table_region.Length;

	if (fsize > backing_file->Size())
	{
		return backing_file->Resize(fsize);
	}	

	return false;
}

std::string VHDXFile::Read(_u32 tr, bool* has_error)
{
	std::string ret = Read(spos, tr, has_error);
	spos += ret.size();
	return ret;
}

std::string VHDXFile::Read(int64 spos, _u32 tr, bool* has_error)
{
	std::string ret;
	ret.resize(tr);

	_u32 rc = Read(spos, &ret[0], tr, has_error);
	if (rc < tr)
		ret.resize(rc);

	return ret;
}

_u32 VHDXFile::Read(char* buffer, _u32 bsize, bool* has_error)
{
	_u32 rc = Read(spos, buffer, bsize, has_error);
	spos += rc;
	return rc;
}

_u32 VHDXFile::Read(int64 spos, char* buffer, _u32 bsize, bool* has_error)
{
	if (spos> dst_size)
	{
		if (has_error != nullptr)
			*has_error = true;

		return 0;
	}
	else if (spos + bsize >= dst_size)
	{
		bsize = static_cast<_u32>(dst_size - spos);
	}

	_u32 read = 0;
	while (bsize - read > 0)
	{
		_u32 block = getBatEntry(spos, block_size, sector_size);

		VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + block;

		if (bat_entry->State == PAYLOAD_BLOCK_FULLY_PRESENT)
		{
			_u32 toread = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - read);

			_u32 rc = file->Read(bat_entry->FileOffsetMB * 1024 * 1024 + spos % block_size,
				buffer + read, toread);

			read += rc;
			spos += rc;

			if (rc < toread)
			{
				if (has_error != nullptr)
					*has_error = true;

				return read;
			}

			continue;
		}

		if (parent.get()==nullptr)
		{
			_u32 toread = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - read);

			if (bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT)
			{
				if (has_error != nullptr)
					*has_error = true;

				return read;
			}
			else if (bat_entry->State == PAYLOAD_BLOCK_NOT_PRESENT ||
				bat_entry->State == PAYLOAD_BLOCK_UNDEFINED ||
				bat_entry->State == PAYLOAD_BLOCK_ZERO ||
				bat_entry->State == PAYLOAD_BLOCK_UNMAPPED)
			{
				toread = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - read);

				memset(buffer+read, 0, toread);
				read += toread;
				spos += toread;
			}
			else
			{
				if (has_error != nullptr)
					*has_error = true;

				return read;
			}
		}
		else
		{
			_u32 toread;
			if (bat_entry->State != PAYLOAD_BLOCK_PARTIALLY_PRESENT)
			{
				toread = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - read);
			}
			else
			{
				toread = (std::min)(sector_size - static_cast<_u32>(spos % sector_size), bsize - read);
			}

			if (bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT)
			{
				bool set;
				if (!isSectorSet(spos, set))
				{
					if (has_error != nullptr)
						*has_error = true;

					return read;
				}

				_u32 rc;
				if (set)
				{
					rc = file->Read(bat_entry->FileOffsetMB * 1024 * 1024 + spos % block_size,
						buffer + read, toread);					
				}
				else
				{
					rc = parent->Read(spos, buffer + read, toread);
				}

				read += rc;
				spos += rc;

				if (rc < toread)
				{
					if (has_error != nullptr)
						*has_error = true;

					return read;
				}
			}
			else if (bat_entry->State == PAYLOAD_BLOCK_UNDEFINED ||
				bat_entry->State == PAYLOAD_BLOCK_ZERO ||
				bat_entry->State == PAYLOAD_BLOCK_UNMAPPED)
			{
				memset(buffer + read, 0, toread);
				read += toread;
				spos += toread;
			}
			else if (bat_entry->State == PAYLOAD_BLOCK_NOT_PRESENT)
			{
				_u32 rc = parent->Read(spos, buffer + read, toread);

				read += rc;
				spos += rc;

				if (rc < toread)
				{
					if (has_error != nullptr)
						*has_error = true;

					return read;
				}
			}
			else
			{
				if (has_error != nullptr)
					*has_error = true;

				return read;
			}
		}
	}
	return read;
}

_u32 VHDXFile::Write(const std::string& tw, bool* has_error)
{
	_u32 rc = Write(spos, tw.data(), static_cast<_u32>(tw.size()), has_error);
	spos += rc;
	return rc;
}

_u32 VHDXFile::Write(int64 spos, const std::string& tw, bool* has_error)
{
	return Write(spos, tw.data(), static_cast<_u32>(tw.size()), has_error);
}

_u32 VHDXFile::Write(int64 spos, const char* buffer, _u32 bsize, bool* has_error)
{
	if (spos > dst_size)
	{
		if (has_error != nullptr)
			*has_error = true;

		return 0;
	}
	else if (spos + bsize >= dst_size)
	{
		bsize = static_cast<_u32>(dst_size - spos);
	}

	if (!data_write_uuid_updated)
	{
		randomGuid(curr_header.DataWriteGuid);

		data_write_uuid_updated = true;

		if (!fast_mode && !updateHeader())
		{
			if (has_error != nullptr)
				*has_error = true;

			return 0;
		}
	}

	_u32 written = 0;
	while (bsize - written > 0)
	{
		int64 block = getBatEntry(spos, block_size, sector_size);
		VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + block;

		if (bat_entry->State == PAYLOAD_BLOCK_FULLY_PRESENT)
		{
			_u32 towrite = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - written);

			_u32 rc = file->Write(bat_entry->FileOffsetMB * 1024 * 1024 + spos % block_size,
				buffer + written, towrite);

			written += rc;
			spos += rc;

			if (rc < towrite)
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}

			continue;
		}

		if (parent.get()==nullptr)
		{
			_u32 towrite = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - written);

			if (bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT)
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}
			else if (bat_entry->State != PAYLOAD_BLOCK_NOT_PRESENT &&
				bat_entry->State != PAYLOAD_BLOCK_UNDEFINED &&
				bat_entry->State != PAYLOAD_BLOCK_ZERO &&
				bat_entry->State != PAYLOAD_BLOCK_UNMAPPED)
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}

			if (!allocateBatBlockFull(block))
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}

			_u32 rc = file->Write(bat_entry->FileOffsetMB * 1024 * 1024 + spos % block_size,
				buffer + written, towrite);

			written += rc;
			spos += rc;

			if (rc < towrite)
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}
		}
		else
		{
			_u32 towrite = (std::min)(block_size - static_cast<_u32>(spos % block_size), bsize - written);

			if (bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT)
			{
				if (!setSector(spos, spos+towrite))
				{
					if (has_error != nullptr)
						*has_error = true;

					return written;
				}
			}
			else if (bat_entry->State != PAYLOAD_BLOCK_NOT_PRESENT &&
				bat_entry->State != PAYLOAD_BLOCK_UNDEFINED &&
				bat_entry->State != PAYLOAD_BLOCK_ZERO &&
				bat_entry->State != PAYLOAD_BLOCK_UNMAPPED)
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}
			else
			{
				if (!allocateBatBlockFull(block))
				{
					if (has_error != nullptr)
						*has_error = true;

					return written;
				}

				bat_entry->State = PAYLOAD_BLOCK_PARTIALLY_PRESENT;

				if (!setSector(spos, spos+towrite))
				{
					if (has_error != nullptr)
						*has_error = true;

					return written;
				}
			}

			
			_u32 rc = file->Write(bat_entry->FileOffsetMB * 1024 * 1024 + spos % block_size,
				buffer + written, towrite);

			written += rc;
			spos += rc;

			if (rc < towrite)
			{
				if (has_error != nullptr)
					*has_error = true;

				return written;
			}
		}
	}
	return written;
}

_i64 VHDXFile::Size(void)
{
	return dst_size;
}

_i64 VHDXFile::RealSize()
{
	return static_cast<_i64>(usedSize());
}

bool VHDXFile::PunchHole(_i64 spos, _i64 size)
{
	return false;
}

bool VHDXFile::Sync()
{
	return syncInt(false);
}

bool VHDXFile::syncInt(bool full)
{
	{
		std::lock_guard<std::mutex> lock(pending_sector_bitmaps_mutex);

		for (_u32 sector_block : pending_sector_bitmaps)
		{
			VhdxBatEntry* sector_bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + sector_block;

			if (sector_bat_entry->State != PAYLOAD_BLOCK_FULLY_PRESENT)
			{
				Server->Log("Sector bitmap bat entry not fully present when syncing", LL_WARNING);
				return false;
			}

			auto it_sector_bitmap = sector_bitmap_bufs.find(sector_block);
			if (it_sector_bitmap == sector_bitmap_bufs.end())
			{
				assert(false);
				return false;
			}

			if (file->Write(sector_bat_entry->FileOffsetMB * 1024 * 1024,
				it_sector_bitmap->second.data(), block_size) != block_size)
			{
				Server->Log("Error writing pending sector bitmap block. " + os_last_error_str(), LL_WARNING);
				return false;
			}
		}

		pending_sector_bitmaps.clear();
	}


	bool retry;
	do
	{
		retry = false;

		std::unique_lock<std::mutex> lock(log_mutex);

		int64 stop_idx = -1;
		if (!fast_mode)
		{
			int64 new_flushed_vhdx_size = file->Size();
			if (flushed_vhdx_size != new_flushed_vhdx_size)
			{
				if (!file->Sync())
				{
					Server->Log("Error syncing VHDX backing file -1. " + os_last_error_str(), LL_WARNING);
					return false;
				}

				flushed_vhdx_size = new_flushed_vhdx_size;
			}

			int64 b_idx = -1;
			int64 last_log_idx = -1;
			for (int64 entry_idx : pending_bat_entries)
			{
				int64 c_b_idx = (entry_idx * sizeof(int64)) / log_sector_size;

				if (b_idx != c_b_idx)
				{
					b_idx = c_b_idx;

					bool full = false;
					if (!logWrite(bat_region.FileOffset + c_b_idx * log_sector_size,
						bat_buf.data() + c_b_idx * log_sector_size, log_sector_size, -1, full))
					{
						if (full)
						{
							stop_idx = entry_idx;
							retry = true;
							break;
						}
						else
						{
							Server->Log("Error logging VHDX BAT write", LL_WARNING);
							return false;
						}
					}
				}
			}

			if (!file->Sync())
			{
				Server->Log("Error syncing VHDX backing file -2. " + os_last_error_str(), LL_WARNING);
				return false;
			}
		}

		int64 b_idx = -1;
		for (auto it = pending_bat_entries.begin(); it != pending_bat_entries.end();)
		{
			int64 entry_idx = *it;

			if (entry_idx == stop_idx)
			{
				break;
			}

			int64 c_b_idx = (entry_idx * sizeof(int64)) / log_sector_size;

			if (b_idx != c_b_idx)
			{
				b_idx = c_b_idx;

				_u32 rc = file->Write(bat_region.FileOffset + c_b_idx * log_sector_size,
					bat_buf.data() + c_b_idx * log_sector_size, log_sector_size);

				if (rc != log_sector_size)
					return false;
			}

			if (stop_idx != -1)
			{
				auto it_prev = it;
				++it;
				pending_bat_entries.erase(it_prev);
			}
			else
			{
				++it;
			}
		}

		if(stop_idx==-1)
			pending_bat_entries.clear();

		if (fast_mode)
		{
			if (!file->Sync())
			{
				Server->Log("Error syncing VHDX backing file -3. " + os_last_error_str(), LL_WARNING);
				return false;
			}
		}

	} while (retry);

	if (full && !fast_mode)
	{
		if (!file->Sync())
		{
			Server->Log("Error syncing VHDX backing file -4. " + os_last_error_str(), LL_WARNING);
			return false;
		}

		zeroGUID(curr_header.LogGuid);

		if (!updateHeader())
			return false;
	}

	return true;
}

void VHDXFile::getDataWriteGUID(VhdxGUID& g)
{
	copyGUID(curr_header.DataWriteGuid, g);
}

bool VHDXFile::createNew()
{
	memset(&curr_header, 0, sizeof(curr_header));
	std::memcpy(&curr_header, "head", 4);
	curr_header.SequenceNumber = 1;
	secureRandomGuid(curr_header.FileWriteGuid);
	secureRandomGuid(curr_header.DataWriteGuid);
	data_write_uuid_updated = true;
	curr_header.Version = 1;
	curr_header.LogOffset = 1 * 1024 * 1024;
	curr_header.LogLength = 1 * 1024 * 1024;
	curr_header.Checksum = crc32c(reinterpret_cast<unsigned char*>(&curr_header), sizeof(curr_header));
	
	log_pos = 0;
	log_start_pos = 0;

	block_size = 1 * 1024 * 1024;
	vhdx_params.BlockSize = block_size;
	sector_size = 512;

	std::vector<char> ident = getFileIdentifier();

	if (file->Write(0, ident.data(), static_cast<_u32>(ident.size())) != ident.size())
	{
		Server->Log("Error writing new ident. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (file->Write(64 * 1024, reinterpret_cast<char*>(&curr_header), sizeof(curr_header)) != sizeof(curr_header))
	{
		Server->Log("Error writing new header 1. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (file->Write(128 * 1024, reinterpret_cast<char*>(&curr_header), sizeof(curr_header)) != sizeof(curr_header))
	{
		Server->Log("Error writing new header 2. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	curr_header_pos = 64 * 1024;

	std::vector<char> region_table = getVhdxRegionTable(dst_size, block_size, sector_size);

	meta_table_region.FileOffset = meta_region_offset;
	meta_table_region.Length = 1 * 1024 * 1024;

	bat_region.FileOffset = bat_table_offset;
	bat_region.Length = getBatLength(dst_size, block_size, sector_size);

	if (file->Write(192 * 1024, region_table.data(), static_cast<_u32>(region_table.size())) != region_table.size())
	{
		Server->Log("Error writing new region table 1. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (file->Write(256 * 1024, region_table.data(), static_cast<_u32>(region_table.size())) != region_table.size())
	{
		Server->Log("Error writing new region table 2. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	std::string parent_data_uuid;
	std::string parent_abs_path;
	std::string parent_rel_path;
	if (parent.get() != nullptr)
	{
		VhdxGUID g;
		parent->getDataWriteGUID(g);
		parent_data_uuid = strGUID(g);

		if (parent_fn.find("..") == 0)
		{
			parent_rel_path = greplace("/", "\\", parent_fn);

			std::string curr_dir = ExtractFilePath(file->getFilename());
			parent_abs_path = parent_fn;

			while (next(parent_abs_path, 0, "..\\"))
			{
				curr_dir = ExtractFilePath(file->getFilename());
				parent_abs_path.erase(0, 3);
			}

			if (!curr_dir.empty() && curr_dir[0] == '\\')
			{
				curr_dir.erase(0, 1);
			}

			parent_abs_path = os_file_prefix(parent_abs_path + "\\" + curr_dir);
		}
		else
		{
			parent_abs_path = os_file_prefix(parent_fn);

			std::string fn = file->getFilename();
			std::string cparent_fn = parent_fn;

			while (fn.find("\\") != std::string::npos
				&& cparent_fn.find("\\") != std::string::npos
				&& getuntil("\\", fn)==getuntil("\\", cparent_fn))
			{
				fn = getafter("\\", fn);
				cparent_fn = getafter("\\", cparent_fn);
			}

			parent_rel_path = cparent_fn;
			for (char ch : fn)
			{
				if (ch == '\\')
					parent_rel_path += "..\\";
			}
		}
	}

	std::vector<char> meta_region = getMetaRegion(dst_size, block_size, sector_size,
		parent_data_uuid, parent_rel_path, parent_abs_path);

	if (file->Write(meta_region_offset, meta_region.data(), static_cast<_u32>(meta_region.size())) != meta_region.size())
	{
		Server->Log("Error writing new metadata region. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (file == backing_file.get() &&
		!backing_file->Resize(bat_region.FileOffset + bat_region.Length + allocate_size_add_size, false))
	{
		Server->Log("Error writing new bat region. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	allocated_size = file->Size();

	next_payload_pos = bat_region.FileOffset + bat_region.Length;

	bat_buf.resize(bat_region.Length);

	return true;
}

bool VHDXFile::updateHeader()
{
	++curr_header.SequenceNumber;

	curr_header.Checksum = 0;
	curr_header.Checksum = crc32c(reinterpret_cast<unsigned char*>(&curr_header), sizeof(curr_header));

	if (file->Write(curr_header_pos, reinterpret_cast<char*>(&curr_header), sizeof(curr_header)) != sizeof(curr_header))
	{
		Server->Log("Error writing VHDX header to pos " + std::to_string(curr_header_pos) + ". " + os_last_error_str());
		return false;
	}

	if (!file->Sync())
	{
		Server->Log("Error syncing VHDX backing file after updating header. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (curr_header_pos == 64 * 1024)
		curr_header_pos = 128 * 1024;
	else
		curr_header_pos = 64 * 1024;

	return true;
}

bool VHDXFile::replayLog()
{
	VHDXFile::LogSequence seq = findLogSequence();

	if (seq.max_sequence == 0)
	{
		Server->Log("Could not find VHDX log sequence -1", LL_WARNING);
		return false;
	}

	if (seq.entries.empty())
	{
		Server->Log("Could not find VHDX log sequence -2", LL_WARNING);
		return false;
	}

	LogEntry head_entry = readLogEntry(file, curr_header.LogGuid, seq.entries[seq.entries.size()-1]);

	if (file->Size() < head_entry.fsize)
	{
		Server->Log("VHDX size smaller than expected from log expected="+std::to_string(head_entry.fsize)
			+" got="+std::to_string(file->Size()), LL_WARNING);
		return false;
	}

	for (int64 entry_pos : seq.entries)
	{
		LogEntry loge = readLogEntry(file, curr_header.LogGuid, entry_pos);

		if (loge.sequence_number == -1)
		{
			Server->Log("Error reading log entry while replaying log", LL_WARNING);
			return false;
		}

		if (file->Size() < loge.fsize)
		{
			Server->Log("VHDX size smaller than expected from log entry expected=" + std::to_string(loge.fsize)
				+ " got=" + std::to_string(file->Size()), LL_WARNING);
			return false;
		}

		for (LogZeroDescriptor& zero_desc : loge.to_zero)
		{
			std::vector<char> zero_buf(zero_desc.ZeroLength);
			if (file->Write(zero_desc.FileOffset, zero_buf.data(), static_cast<_u32>(zero_buf.size())) != zero_buf.size())
			{
				Server->Log("Error writing zeroes from log. " + os_last_error_str(), LL_WARNING);
				return false;
			}
		}

		for (LogData& log_data : loge.to_write)
		{
			if (file->Write(log_data.offset, log_data.data, sizeof(log_data.data)) != sizeof(log_data.data))
			{
				Server->Log("Error writing data from log. " + os_last_error_str(), LL_WARNING);
				return false;
			}
		}

		log_sequence_num = loge.sequence_number + 1;
	}
	
	int64 new_fsize = -1;
	if (file->Size() < head_entry.new_fsize &&
		file == backing_file.get())
	{
		if (backing_file->Resize(head_entry.new_fsize, false))
			new_fsize = head_entry.new_fsize;
	}

	if (!file->Sync())
	{
		Server->Log("Error syncing after writing log. " + os_last_error_str());
		return false;
	}

	if (new_fsize >= 0)
		flushed_vhdx_size = new_fsize;

	zeroGUID(curr_header.LogGuid);

	return updateHeader();
}

bool VHDXFile::readHeader()
{
	std::string ident = file->Read(0LL, 8);

	if (ident != "vhdxfile")
	{
		Server->Log("VHDX header tag wrong", LL_WARNING);
		return false;
	}

	VhdxHeader header1, header2;

	if (file->Read(64LL * 1024, reinterpret_cast<char*>(&header1), sizeof(header1)) != sizeof(header1))
	{
		Server->Log("Could not read VHDX header 1. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (file->Read(128LL * 1024, reinterpret_cast<char*>(&header2), sizeof(header2)) != sizeof(header2))
	{
		Server->Log("Could not read VHDX header 2. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	VhdxHeader* sel_header = nullptr;

	if (checkHeader(file, header1))
	{
		sel_header = &header1;
		curr_header_pos = 128LL * 1024;
	}

	if (checkHeader(file, header2) &&
		header2.SequenceNumber > header1.SequenceNumber)
	{
		sel_header = &header2;
		curr_header_pos = 64LL * 1024;
	}

	if (sel_header == nullptr)
	{
		Server->Log("Both VHDX headers are invalid", LL_WARNING);
		return false;
	}

	std::memcpy(&curr_header, sel_header, sizeof(curr_header));

	return true;
}

bool VHDXFile::readRegionTable(int64 off)
{
	std::vector<char> region_buf(64 * 1024);

	if (file->Read(off, region_buf.data(), static_cast<_u32>(region_buf.size())) != region_buf.size())
	{
		Server->Log("Error reading VHDX region table. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (std::string(region_buf.data(), 4) != "regi")
	{
		Server->Log("VHDX region table tag wrong", LL_WARNING);
		return false;
	}

	VhdxRegionTableHeader* header = reinterpret_cast<VhdxRegionTableHeader*>(region_buf.data());

	_u32 ccrc = header->Checksum;

	header->Checksum = 0;

	if (crc32c(reinterpret_cast<unsigned char*>(region_buf.data()), region_buf.size()) != ccrc)
	{
		Server->Log("VHDX region table checksum wrong", LL_WARNING);
		return false;
	}

	VhdxGUID meta_table_guid;
	makeMetaTableGUID(meta_table_guid);
	VhdxGUID bat_guid;
	makeBatGUID(bat_guid);

	unsigned int found = 0;

	for (_u32 i = 0; i < header->EntryCount; ++i)
	{
		VhdxRegionTableEntry* entry = reinterpret_cast<VhdxRegionTableEntry*>(region_buf.data() + sizeof(VhdxRegionTableHeader)
			+ i*sizeof(VhdxRegionTableEntry));

		if (equalsGUID(entry->Guid, meta_table_guid))
		{
			std::memcpy(&meta_table_region, entry, sizeof(meta_table_region));
			if (found & 1)
			{
				Server->Log("Found metadata table region entry twice", LL_WARNING);
				return false;
			}
			found |= 1;
		}
		else if (equalsGUID(entry->Guid, bat_guid))
		{
			std::memcpy(&bat_region, entry, sizeof(bat_region));
			if (found & 2)
			{
				Server->Log("Found BAT table region entry twice", LL_WARNING);
				return false;
			}
			found |= 2;
		}
		else
		{
			Server->Log("Unknown region table entry " + strGUID(entry->Guid), LL_WARNING);
			return false;
		}
	}

	if ((found ^ (1 | 2)) != 0)
	{
		Server->Log("Did not find required region table entry. Found="+std::to_string(found), LL_WARNING);
		return false;
	}

	return true;
}

bool VHDXFile::readBat()
{
	bat_buf.resize(bat_region.Length);

	const _u32 read_size = 512 * 1024;

	for (_u32 i = 0; i < bat_region.Length; i += read_size)
	{
		_u32 toread = (std::min)(read_size, bat_region.Length - i);
		if (file->Read(bat_region.FileOffset + i, bat_buf.data() + i, toread) != toread)
		{
			Server->Log("Error reading VHDX BAT at pos " + std::to_string(bat_region.FileOffset + i)
				+ " toread " + std::to_string(toread) + ". " + os_last_error_str(), LL_WARNING);
			return false;
		}
	}

	return true;
}

bool VHDXFile::readMeta()
{
	std::vector<char> meta_table(64 * 1024);

	if (meta_table_region.Length < 64 * 1024)
	{
		Server->Log("Meta table region length smaller than 64KiB", LL_WARNING);
		return false;
	}

	if (file->Read(meta_table_region.FileOffset, meta_table.data(), static_cast<_u32>(meta_table.size())) != meta_table.size())
	{
		Server->Log("Error reading VHDX meta table from pos " +
			std::to_string(meta_table_region.FileOffset) + ". " + os_last_error_str(), LL_WARNING);
		return false;
	}

	VhdxMetadataTableHeader* table_header = reinterpret_cast<VhdxMetadataTableHeader*>(meta_table.data());

	std::string ident(meta_table.data(), 8);

	if (ident != "metadata")
	{
		Server->Log("Meta table ident wrong", LL_WARNING);
		return false;
	}

	sector_size = 0;
	physical_sector_size = 0;
	vhdx_params.BlockSize = 0;
	dst_size = -1;

	VhdxGUID parent_linkage_guid = {};
	VhdxGUID file_parameters_guid, virtual_disk_size_guid, logical_sector_size_guid,
		physical_sector_size_guid, virtual_disk_id_guid, parent_locator_guid;

	makeFileParametersGUID(file_parameters_guid);
	makeVirtualDiskSizeGUID(virtual_disk_size_guid);
	makeLogicalSectorSizeGUID(logical_sector_size_guid);
	makePhysicalSectorSizeGUID(physical_sector_size_guid);
	makeVirtualDiskIdGUID(virtual_disk_id_guid);
	makeParentLocatorGUID(parent_locator_guid);

	std::string rel_parent_path;
	std::string volume_parent_path;
	std::string absolute_win32_parent_path;

	for (unsigned short i = 0; i < table_header->EntryCount; ++i)
	{
		if (32 + i * 32 + 32 > meta_table.size())
		{
			Server->Log("Meta table not large enough", LL_WARNING);
			return false;
		}

		VhdxMetadataTableEntry* table_entry = reinterpret_cast<VhdxMetadataTableEntry*>(meta_table.data() + 32 + i * 32);

		if (table_entry->Offset < 64 * 1024)
		{
			Server->Log("Meta table offset wrong: " + std::to_string(table_entry->Offset), LL_WARNING);
			return false;
		}
		if (table_entry->Offset + table_entry->Length > meta_table_region.Length)
		{
			Server->Log("Meta table offset+length wrong: " + std::to_string(table_entry->Offset + table_entry->Length), LL_WARNING);
			return false;
		}

		std::vector<char> entry_buf(table_entry->Length);

		if (file->Read(meta_table_region.FileOffset + table_entry->Offset,
			entry_buf.data(), static_cast<_u32>(entry_buf.size())) != entry_buf.size())
		{
			Server->Log("Error reading meta table entry. " + os_last_error_str(), LL_WARNING);
			return false;
		}

		if (equalsGUID(table_entry->ItemId, file_parameters_guid))
		{
			if (entry_buf.size() < sizeof(VhdxFileParameters))
			{
				Server->Log("VhdxFileParameters entry not large enough", LL_WARNING);
				return false;
			}

			std::memcpy(&vhdx_params, entry_buf.data(), sizeof(vhdx_params));
		}
		else if (equalsGUID(table_entry->ItemId, virtual_disk_size_guid))
		{
			if (entry_buf.size() < sizeof(VhdxVirtualDiskSize))
			{
				Server->Log("VhdxVirtualDiskSize entry not large enough", LL_WARNING);
				return false;
			}

			VhdxVirtualDiskSize* virtual_disk_size = reinterpret_cast<VhdxVirtualDiskSize*>(entry_buf.data());

			dst_size = virtual_disk_size->VirtualDiskSize;
		}
		else if (equalsGUID(table_entry->ItemId, physical_sector_size_guid))
		{
			if (entry_buf.size() < sizeof(VhdxPhysicalDiskSectorSize))
			{
				Server->Log("VhdxPhysicalDiskSectorSize entry not large enough", LL_WARNING);
				return false;
			}

			VhdxPhysicalDiskSectorSize* physical_disk_sector_size = reinterpret_cast<VhdxPhysicalDiskSectorSize*>(entry_buf.data());

			physical_sector_size = physical_disk_sector_size->PhysicalSectorSize;
		}
		else if (equalsGUID(table_entry->ItemId, logical_sector_size_guid))
		{
			if (entry_buf.size() < sizeof(VhdxVirtualDiskLogicalSectorSize))
			{
				Server->Log("VhdxVirtualDiskLogicalSectorSize entry not large enough", LL_WARNING);
				return false;
			}

			VhdxVirtualDiskLogicalSectorSize* logical_disk_sector_size = reinterpret_cast<VhdxVirtualDiskLogicalSectorSize*>(entry_buf.data());

			sector_size = logical_disk_sector_size->LogicalSectorSize;
		}
		else if (equalsGUID(table_entry->ItemId, virtual_disk_id_guid))
		{
			if (entry_buf.size() < sizeof(VhdxVirtualDiskId))
			{
				Server->Log("VhdxVirtualDiskId entry not large enough", LL_WARNING);
				return false;
			}

			VhdxVirtualDiskId* virtual_disk_id = reinterpret_cast<VhdxVirtualDiskId*>(entry_buf.data());
		}
		else if (equalsGUID(table_entry->ItemId, parent_locator_guid))
		{
			if (entry_buf.size() < sizeof(VhdxParentLocatorHeader))
			{
				Server->Log("Parent locator entry not large enough", LL_WARNING);
				return false;
			}

			VhdxParentLocatorHeader* parent_locator_header = reinterpret_cast<VhdxParentLocatorHeader*>(entry_buf.data());

			VhdxGUID vhdx_parent_locator_guid;
			makeVhdxParentLocatorGUID(vhdx_parent_locator_guid);

			if (!equalsGUID(parent_locator_header->LocatorType, vhdx_parent_locator_guid))
			{
				Server->Log("Unknown parent locator type " + strGUID(parent_locator_header->LocatorType), LL_WARNING);
				return false;
			}

			for (unsigned short i = 0; i < parent_locator_header->KeyValueCount; ++i)
			{
				VhdxParentLocatorEntry* parent_locator_entry = reinterpret_cast<VhdxParentLocatorEntry*>(entry_buf.data() + 20 + i * 12);

				if (parent_locator_entry->KeyOffset + parent_locator_entry->KeyLength >= entry_buf.size()
					|| parent_locator_entry->KeyOffset>10*1024*1024)
				{
					Server->Log("Parent locator entry key offset not plausible: "+std::to_string(parent_locator_entry->KeyOffset)+
						" length: "+std::to_string(parent_locator_entry->KeyLength),
						LL_WARNING);
					return false;
				}

				if (parent_locator_entry->ValueOffset + parent_locator_entry->ValueLength >= entry_buf.size()
					|| parent_locator_entry->ValueOffset > 10 * 1024 * 1024)
				{
					Server->Log("Parent locator entry key offset not plausible: " + std::to_string(parent_locator_entry->ValueOffset)+
						" length: " + std::to_string(parent_locator_entry->ValueLength),
						LL_WARNING);
					return false;
				}

				std::string key_vw(entry_buf.data() + parent_locator_entry->KeyOffset, parent_locator_entry->KeyLength);
				std::string value_vw(entry_buf.data() + parent_locator_entry->ValueOffset, parent_locator_entry->ValueLength);

				std::string key_v = Server->ConvertFromUTF16(key_vw);
				std::string value_v = Server->ConvertFromUTF16(value_vw);

				if (key_v == "parent_linkage")
				{
					if (!parseStrGuid(value_v, parent_linkage_guid))
					{
						Server->Log("Error parsing parent linkage GUID " + value_v, LL_WARNING);
						return false;
					}
				}
				else if (key_v == "relative_path")
				{
					rel_parent_path = value_v;
				}
				else if (key_v == "volume_path")
				{
					volume_parent_path = value_v;
				}
				else if (key_v == "absolute_win32_path")
				{
					absolute_win32_parent_path = value_v;
				}
			}
		}
		else if(table_entry->IsRequired)
		{
			Server->Log("Required table entry " + strGUID(table_entry->ItemId) + " not suppoerted", LL_WARNING);
			return false;
		}
	}

	if (sector_size == 0 ||
		physical_sector_size == 0 ||
		vhdx_params.BlockSize == 0 ||
		dst_size == -1)
	{
		Server->Log("Missing VHDX parameter. sector_size=" + std::to_string(sector_size) +
			" physical_sector_size=" + std::to_string(physical_sector_size) +
			" vhdx_params.BlockSize=" + std::to_string(vhdx_params.BlockSize)+
			" dst_size=" + std::to_string(dst_size), LL_WARNING);
		return false;
	}

	block_size = vhdx_params.BlockSize;

	if (vhdx_params.HasParent)
	{
		if (isZeroGUID(parent_linkage_guid))
		{
			Server->Log("Parent linkage GUID is zero", LL_WARNING);
			return false;
		}

		if (FileExists(absolute_win32_parent_path))
		{
			parent.reset(new VHDXFile(absolute_win32_parent_path,
				true, 0));
		}
		else if (FileExists(rel_parent_path))
		{
			parent.reset(new VHDXFile(rel_parent_path,
				true, 0));
		}

		if (parent.get() == nullptr ||
			!parent->isOpen())
		{
			Server->Log("Could not open parent vhdx at \"" + absolute_win32_parent_path + "\" or "
				"\"" + rel_parent_path + "\"", LL_WARNING);
			return false;
		}

		VhdxGUID dwg;
		parent->getDataWriteGUID(dwg);

		if (!equalsGUID(dwg, parent_linkage_guid))
		{
			Server->Log("Parent linkage GUID differs. Got " + strGUID(dwg) + " expected " + strGUID(parent_linkage_guid), LL_WARNING);
			return false;
		}
	}

	return true;
}

bool VHDXFile::allocateBatBlockFull(int64 block)
{
	VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + block;

	bat_entry->State = PAYLOAD_BLOCK_FULLY_PRESENT;

	int64 new_pos = next_payload_pos.fetch_add(block_size, std::memory_order_relaxed);

	if (new_pos > file->Size())
	{
		allocated_size = new_pos + block_size + allocate_size_add_size;

		if (file == backing_file.get() &&
			!backing_file->Resize(allocated_size, false))
		{
			Server->Log("Error resizing backing file to new allocated size " 
				+ std::to_string(allocated_size) + ". " + os_last_error_str(),
				LL_WARNING);
			return false;
		}
	}

	assert(new_pos % (1 * 1024 * 1024) == 0);
	bat_entry->FileOffsetMB = new_pos / (1 * 1024 * 1024);
	bat_entry->Reserved = 0;

	{
		std::unique_lock<std::mutex> lock(log_mutex);
		pending_bat_entries.insert(block);
	}

	return true;
}

void VHDXFile::calcNextPayloadPos()
{
	int64 next_pos = 1 * 1024 * 1024;

	next_pos = (std::max)(next_pos,
		static_cast<int64>(bat_region.FileOffset + bat_region.Length));

	next_pos = (std::max)(next_pos,
		static_cast<int64>(curr_header.LogOffset + curr_header.LogLength));

	next_pos = (std::max)(next_pos,
		static_cast<int64>(meta_table_region.FileOffset + meta_table_region.Length));

	_u32 bat_entries = getBatEntries(dst_size, block_size, sector_size);

	for (_u32 i = 0; i < bat_entries; ++i)
	{
		VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + i;
		next_pos = (std::max)(next_pos,
			static_cast<int64>(bat_entry->FileOffsetMB*1024*1024 + block_size));
	}

	next_payload_pos = next_pos;
}

bool VHDXFile::open(const std::string& fn, bool compress, size_t compress_n_threads)
{
	backing_file.reset(Server->openFile(fn, read_only ? MODE_READ : MODE_RW_CREATE));

	if (backing_file.get() == nullptr)
	{
		Server->Log("Error opening VHDX backing file at \"" 
			+ fn + "\". " + os_last_error_str(), LL_WARNING);
		return false;
	}

	if (backing_file->Size() == 0)
	{
		if (read_only)
		{
			Server->Log("Read only vhdx file has zero size", LL_WARNING);
			return false;
		}

		if (compress)
		{
			compressed_file = std::make_unique<CompressedFile>(backing_file.get(),
				false, read_only, compress_n_threads);

			if (compressed_file->hasError())
			{
				Server->Log("Error opening VHDX compressed file -1", LL_WARNING);
				return false;
			}

			file = compressed_file.get();
		}
		else
		{
			file = backing_file.get();
		}

		return createNew();
	}
	else
	{
		if (check_if_compressed())
		{
			compressed_file = std::make_unique<CompressedFile>(backing_file.get(),
				true, read_only, compress_n_threads);

			if (compressed_file->hasError())
			{
				Server->Log("Error opening VHDX compressed file -2", LL_WARNING);
				return false;
			}

			file = compressed_file.get();
		}
		else
		{
			file = backing_file.get();
		}

		if (!readHeader())
		{
			Server->Log("Error reading VHDX header", LL_WARNING);
			return false;
		}

		if (!readRegionTable(192 * 1024) &&
			!readRegionTable(256 * 1024))
		{
			Server->Log("Error reading any VHDX region table", LL_WARNING);
			return false;
		}

		if (!readBat())
		{
			Server->Log("Error reading any VHDX bat", LL_WARNING);
			return false;
		}

		if (!readMeta())
		{
			Server->Log("Error reading any VHDX metadata", LL_WARNING);
			return false;
		}

		if (read_only && !isZeroGUID(curr_header.LogGuid))
		{
			Server->Log("VHDX is opened read only but has log entries", LL_WARNING);
			return false;
		}

		if (!read_only && !isZeroGUID(curr_header.LogGuid))
		{
			if (!replayLog())
			{
				Server->Log("Error replaying VHDX log", LL_WARNING);
				return false;
			}
		}

		calcNextPayloadPos();

		allocated_size = backing_file->Size();

		secureRandomGuid(curr_header.FileWriteGuid);

		flushed_vhdx_size = allocated_size;

		if (!fast_mode && 
			!read_only &&
			!updateHeader())
		{
			return false;
		}
		
		return true;
	}
}

bool VHDXFile::has_sector_int(int64 spos)
{
	if (spos >= dst_size)
		return true;

	_u32 block = getBatEntry(spos, block_size, sector_size);

	VhdxBatEntry* bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + block;

	return bat_entry->State == PAYLOAD_BLOCK_FULLY_PRESENT ||
		bat_entry->State == PAYLOAD_BLOCK_PARTIALLY_PRESENT;
}

VHDXFile::LogSequence VHDXFile::findLogSequence()
{
	LogSequence max_seq;
	max_seq.max_sequence = 0;
	for (uint64 log_pos = curr_header.LogOffset;
		log_pos < curr_header.LogOffset + curr_header.LogLength;)
	{
		LogSequence seq = findLogSequence(log_pos);
		if (seq.max_sequence > max_seq.max_sequence)
			max_seq = seq;
	}

	return max_seq;
}

VHDXFile::LogSequence& VHDXFile::validateSequence(LogSequence& seq)
{
	if (seq.entries.empty())
		return seq;

	LogEntry head = readLogEntry(file, curr_header.LogGuid,
		static_cast<int64>(seq.entries[seq.entries.size() - 1]));

	if (head.sequence_number == -1)
	{
		seq.entries.clear();
		return seq;
	}

	if (curr_header.LogOffset + head.tail_pos != seq.entries[0])
	{
		seq.entries.clear();
		return seq;
	}

	return seq;
}

VHDXFile::LogSequence VHDXFile::findLogSequence(uint64& off)
{
	int64 expected_seq = 0;
	VHDXFile::LogSequence seq;

	while (true)
	{
		LogEntry loge = readLogEntry(file, curr_header.LogGuid, static_cast<int64>(off));

		if (loge.sequence_number == -1)
		{
			off += 4096;
			return validateSequence(seq);
		}

		if (expected_seq != 0 && expected_seq != loge.sequence_number)
		{
			return validateSequence(seq);
		}

		seq.entries.push_back(off);
		seq.max_sequence = loge.sequence_number;
		seq.fsize = loge.fsize;

		off += loge.length;
		off = (off - curr_header.LogOffset) % curr_header.LogLength + curr_header.LogOffset;

		expected_seq = loge.sequence_number + 1;
	}
}

bool VHDXFile::logWrite(int64 off, const char* buf, size_t bsize,
	int64 new_dst_size, bool& full)
{
	if (bsize > 126 * log_sector_size)
	{
		assert(false);
		return false;
	}

	assert(bsize % log_sector_size == 0);

	if (isZeroGUID(curr_header.LogGuid))
	{
		randomGuid(curr_header.LogGuid);
		log_pos = 0;
		log_start_pos = 0;

		if (!updateHeader())
			return false;
	}

	size_t desc_count = roundUp(bsize, size_t{ log_sector_size }) / log_sector_size;

	std::vector<char> log_entry(log_sector_size + roundUp(bsize, size_t{ log_sector_size } ) );

	if (log_pos + log_entry.size() > curr_header.LogLength)
	{
		full = true;
		return false;
	}

	LogEntryHeader* header = reinterpret_cast<LogEntryHeader*>(log_entry.data());

	std::memcpy(&header->signature, "loge", 4);

	header->Checksum = 0;
	header->EntryLength = static_cast<_u32>(log_entry.size());
	header->DescriptorCount = static_cast<_u32>(desc_count);
	header->Tail = static_cast<_u32>(log_start_pos);
	header->FlushedFileOffset = flushed_vhdx_size;
	if (new_dst_size <= 0)
		header->LastFileOffset = header->FlushedFileOffset;
	else
		header->LastFileOffset = new_dst_size;
	copyGUID(curr_header.LogGuid, header->LogGuid);
	header->SequenceNumber = log_sequence_num;

	++log_sequence_num;

	for (size_t i = 0; i < desc_count; ++i)
	{
		LogDataDescriptor* data_desc = reinterpret_cast<LogDataDescriptor*>(log_entry.data() + 64 + i * 32);

		std::memcpy(&data_desc->signature, "desc", 4);
		data_desc->FileOffset = off + i * log_sector_size;
		std::memcpy(data_desc->LeadingBytes, buf + i * log_sector_size, 8);
		std::memcpy(data_desc->TrailingBytes, buf + i * log_sector_size + (log_sector_size - 4), 4);
		data_desc->SequenceNumber = header->SequenceNumber;
	}

	for (size_t i = 0; i < bsize; i += log_sector_size)
	{
		LogDataSector* data_sec = reinterpret_cast<LogDataSector*>(log_entry.data() + log_sector_size + i * log_sector_size);

		std::memcpy(&data_sec->signature, "data", 4);
		SSequence seq;
		seq.QuadPart = header->SequenceNumber;
		data_sec->SequenceLow = seq.LowPart;
		data_sec->SequenceHigh = seq.HighPart;
		std::memcpy(data_sec->data, buf + i + 8, log_sector_size - 8 - 4);
	}

	header->Checksum = crc32c(reinterpret_cast<unsigned char*>(log_entry.data()), log_entry.size());

	if (file->Write(curr_header.LogOffset + log_pos, log_entry.data(),
		static_cast<_u32>(log_entry.size())) != log_entry.size())
	{
		Server->Log("Error writing VHDX log entry. " + os_last_error_str(), LL_WARNING);
		return false;
	}

	log_pos += log_entry.size();

	return true;
}

char* VHDXFile::getSectorBitmap(_u32 sector_block, uint64 FileOffsetMB)
{
	std::unique_lock<std::mutex> lock(sector_bitmap_mutex);
	auto it_sector_bitmap = sector_bitmap_bufs.find(sector_block);
	if (it_sector_bitmap == sector_bitmap_bufs.end())
	{
		lock.unlock();

		std::vector<char> sector_bitmap_buf(block_size);

		if (file->Read(FileOffsetMB * 1024 * 1024,
			sector_bitmap_buf.data(), 
			static_cast<_u32>(sector_bitmap_buf.size())) != block_size)
		{
			Server->Log("Reading sector bitmap from mb offset " + std::to_string(FileOffsetMB)
				+ " failed. " + os_last_error_str(), LL_ERROR);
			return nullptr;
		}

		lock.lock();

		if (sector_bitmap_bufs.find(sector_block) == sector_bitmap_bufs.end())
		{
			sector_bitmap_bufs[sector_block] = sector_bitmap_buf;
		}

		it_sector_bitmap = sector_bitmap_bufs.find(sector_block);

		lock.unlock();
	}

	return it_sector_bitmap->second.data();
}

char* VHDXFile::addZeroBitmap(_u32 sector_block)
{
	std::unique_lock<std::mutex> lock(sector_bitmap_mutex);
	auto it_sector_bitmap = sector_bitmap_bufs.find(sector_block);
	if (it_sector_bitmap == sector_bitmap_bufs.end())
	{
		std::vector<char> sector_bitmap_buf(block_size);
		return sector_bitmap_bufs.insert(std::make_pair(sector_block, sector_bitmap_buf)).first->second.data();
	}

	return it_sector_bitmap->second.data();
}

bool VHDXFile::isSectorSet(int64 spos, bool& set)
{
	_u32 sector_block = getSectorBitmapEntry(spos, block_size, sector_size);
	VhdxBatEntry* sector_bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + sector_block;

	if (sector_bat_entry->State != PAYLOAD_BLOCK_FULLY_PRESENT)
	{
		Server->Log("Sector bitmap " + std::to_string(sector_block) + " not fully present", LL_WARNING);
		return false;
	}

	char* sector_bitmap = getSectorBitmap(sector_block, sector_bat_entry->FileOffsetMB);

	if (sector_bitmap == nullptr)
	{
		Server->Log("Error reading sector bitmap of sector block " + std::to_string(sector_block), LL_ERROR);
		return false;
	}

	set = isSectorSetInt(sector_bitmap, spos, block_size, sector_size);

	return true;
}

bool VHDXFile::setSector(int64 spos)
{
	return setSector(spos, spos + sector_size);
}

bool VHDXFile::setSector(int64 start, int64 end)
{
	_u32 sector_block = getSectorBitmapEntry(start, block_size, sector_size);

	VhdxBatEntry* sector_bat_entry = reinterpret_cast<VhdxBatEntry*>(bat_buf.data()) + sector_block;

	if (sector_bat_entry->State != PAYLOAD_BLOCK_FULLY_PRESENT &&
		sector_bat_entry->State != PAYLOAD_BLOCK_NOT_PRESENT)
	{
		Server->Log("Sector bitmap " + std::to_string(sector_block) + " wrong state "
			+std::to_string(sector_bat_entry->State), LL_WARNING);
		return false;
	}

	char* sector_bitmap;
	if (sector_bat_entry->State == PAYLOAD_BLOCK_NOT_PRESENT)
	{
		if (!allocateBatBlockFull(sector_block))
			return false;

		sector_bitmap = addZeroBitmap(sector_block);
	}
	else
	{
		sector_bitmap = getSectorBitmap(sector_block, sector_bat_entry->FileOffsetMB);
	}

	if (sector_bitmap == nullptr)
		return false;

	setSectorInt(sector_bitmap, start, end, block_size, sector_size);

	std::lock_guard<std::mutex> lock(pending_sector_bitmaps_mutex);

	pending_sector_bitmaps.insert(sector_block);

	return true;
}

bool VHDXFile::check_if_compressed()
{
	const char header_magic[] = "URBACKUP COMPRESSED FILE";
	std::string magic = backing_file->Read(0LL, sizeof(header_magic) - 1);

	return magic == std::string(header_magic);
}

bool VHDXFile::has_block(bool use_parent)
{
	if (!has_sector_int(spos))
	{
		if (use_parent && parent.get() != nullptr)
			return parent->has_block(true);

		return false;
	}

	return true;
}
