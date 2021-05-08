#pragma once

#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "IVHDFile.h"

#include <memory>
#include <atomic>
#include <mutex>
#include <set>

class CompressedFile;

typedef char VhdxGUID[16];

#pragma pack(1)
struct VhdxHeader
{
	_u32 Signature;
	_u32 Checksum;
	uint64 SequenceNumber;
	VhdxGUID FileWriteGuid;
	VhdxGUID DataWriteGuid;
	VhdxGUID LogGuid;
	unsigned short LogVersion;
	unsigned short Version;
	_u32 LogLength;
	uint64 LogOffset;
	char Reserved[4016];
};

struct VhdxRegionTableEntry
{
	VhdxGUID Guid;
	uint64 FileOffset;
	_u32 Length;
	_u32 Required : 1;
	_u32 Reserved : 31;
};

struct VhdxFileParameters
{
	_u32 BlockSize;
	_u32 LeaveBlocksAllocated : 1;
	_u32 HasParent : 1;
	_u32 Reserved : 30;
};

struct VhdxBatEntry
{
	uint64 State : 3;
	uint64 Reserved : 17;
	uint64 FileOffsetMB : 44;
};
#pragma pack()

class VHDXFile : public IVHDFile
{
public:

	VHDXFile(const std::string& fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize = 2 * 1024 * 1024, bool fast_mode = false, bool compress = false, size_t compress_n_threads = 0);
	VHDXFile(const std::string& fn, const std::string& parent_fn, bool pRead_only, bool fast_mode = false, bool compress = false, uint64 pDstsize = 0, size_t compress_n_threads = 0);
	~VHDXFile();

	virtual bool Seek(_i64 offset) override;
	virtual bool Read(char* buffer, size_t bsize, size_t& read) override;
	virtual _u32 Write(const char* buffer, _u32 bsize, bool* has_error = NULL) override;
	virtual bool isOpen(void) override;
	virtual uint64 getSize(void) override;
	virtual uint64 usedSize(void) override;
	virtual std::string getFilename(void) override;
	virtual bool has_sector(_i64 sector_size = -1) override;
	virtual bool this_has_sector(_i64 sector_size = -1) override;
	virtual unsigned int getBlocksize() override;
	virtual bool finish() override;
	virtual bool trimUnused(_i64 fs_offset, _i64 trim_blocksize, ITrimCallback* trim_callback) override;
	virtual bool syncBitmap(_i64 fs_offset) override;
	virtual bool makeFull(_i64 fs_offset, IVHDWriteCallback* write_callback) override;
	virtual bool setUnused(_i64 unused_start, _i64 unused_end) override;
	virtual bool setBackingFileSize(_i64 fsize) override;

	virtual std::string Read(_u32 tr, bool* has_error = NULL) override;
	virtual std::string Read(int64 spos, _u32 tr, bool* has_error = NULL) override;
	virtual _u32 Read(char* buffer, _u32 bsize, bool* has_error = NULL) override;
	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool* has_error = NULL) override;
	virtual _u32 Write(const std::string& tw, bool* has_error = NULL) override;
	virtual _u32 Write(int64 spos, const std::string& tw, bool* has_error = NULL) override;
	virtual _u32 Write(int64 spos, const char* buffer, _u32 bsiz, bool* has_error = NULL) override;
	virtual _i64 Size(void) override;
	virtual _i64 RealSize() override;
	virtual bool PunchHole(_i64 spos, _i64 size) override;
	virtual bool Sync() override;

	void getDataWriteGUID(VhdxGUID& g);

private:
	bool createNew();
	bool updateHeader();
	bool replayLog();
	bool readHeader();
	bool readRegionTable(int64 off);
	bool readBat();
	bool readMeta();
	bool allocateBatBlockFull(int64 block);
	void calcNextPayloadPos();
	bool open(const std::string& fn, bool compress, size_t compress_n_threads);
	bool syncInt(bool full);

	bool has_sector_int(int64 spos);

	struct LogSequence
	{
		std::vector<uint64> entries;
		int64 max_sequence = -1;
		int64 fsize = -1;
		int64 new_fsize = -1;
	};

	LogSequence findLogSequence();
	LogSequence& validateSequence(LogSequence& seq);
	LogSequence findLogSequence(uint64& off);

	bool logWrite(int64 off, const char* buf, size_t bsize, int64 new_dst_size, bool& full);

	char* getSectorBitmap(_u32 sector_block, uint64 FileOffsetMB);
	char* addZeroBitmap(_u32 sector_block);

	bool isSectorSet(int64 spos, bool& set);
	bool setSector(int64 spos);
	bool setSector(int64 start, int64 end);

	bool check_if_compressed();

	bool has_block(bool use_parent);

	VhdxHeader curr_header;
	int64 curr_header_pos;

	VhdxRegionTableEntry meta_table_region;
	VhdxRegionTableEntry bat_region;

	VhdxFileParameters vhdx_params;

	std::vector<char> bat_buf;
	std::set<int64> pending_bat_entries;

	std::unique_ptr<IFsFile> backing_file;
	IFile* file;
	std::unique_ptr<CompressedFile> compressed_file;
	int64 allocated_size;
	bool is_open = false;
	int64 dst_size;
	int64 flushed_vhdx_size;
	bool data_write_uuid_updated = false;

	std::mutex log_mutex;

	_u32 sector_size;
	_u32 physical_sector_size;
	_u32 block_size;

	std::atomic<int64> next_payload_pos;

	int64 log_start_pos;
	int64 log_pos;
	int64 log_sequence_num = 1;

	int64 spos = 0;

	bool fast_mode;
	bool read_only;
	bool finished = false;

	std::unique_ptr<VHDXFile> parent;
	std::string parent_fn;

	std::mutex sector_bitmap_mutex;
	std::map<_u32, std::vector<char> > sector_bitmap_bufs;

	std::mutex pending_sector_bitmaps_mutex;
	std::set<_u32> pending_sector_bitmaps;
};
