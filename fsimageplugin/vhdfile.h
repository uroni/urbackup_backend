#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "IVHDFile.h"

#ifndef sun
#pragma pack(push)
#endif
#pragma pack(1)

struct VHDFooter
{
	char cookie[8];
	unsigned int features;
	unsigned int format_version;
	uint64 data_offset;
	unsigned int timestamp;
	char creator_application[4];
	unsigned int creator_version;
	unsigned int creator_os;
	uint64 original_size;
	uint64 current_size;
	unsigned int disk_geometry;
	unsigned int disk_type;
	unsigned int checksum;
	char uid[16];
	char saved_state;
	char reserved[427];
};

struct VHDParentLocator
{
	unsigned int platform_code;
	unsigned int platform_space;
	unsigned int platform_length;
	unsigned int reserved;
	uint64 platform_offset;
};

struct VHDDynamicHeader
{
	char cookie[8];
	uint64 dataoffset;
	uint64 tableoffset;
	unsigned int header_version;
	unsigned int table_entries;
	unsigned int blocksize;
	unsigned int checksum;
	char parent_uid[16];
	unsigned int parent_timestamp;
	unsigned int reserved;
	char parent_unicodename[512];
	VHDParentLocator parentlocator[8];
	char reserved2[256];
};

#ifndef sun
#pragma pack(pop)
#else
#pragma pack()
#endif

class CompressedFile;

class VHDFile : public IVHDFile, public IFile
{
public:
	VHDFile(const std::string &fn, bool pRead_only, uint64 pDstsize, unsigned int pBlocksize=2*1024*1024, bool fast_mode=false, bool compress=false);
	VHDFile(const std::string &fn, const std::string &parent_fn, bool pRead_only, bool fast_mode=false, bool compress=false);
	~VHDFile();

	virtual std::string Read(_u32 tr, bool *has_error=NULL);
	virtual std::string Read(int64 spos, _u32 tr, bool *has_error = NULL);
	virtual _u32 Read(char* buffer, _u32 bsize, bool *has_error=NULL);
	virtual _u32 Read(int64 spos, char* buffer, _u32 bsize, bool *has_error = NULL);
	virtual _u32 Write(const std::string &tw, bool *has_error=NULL);
	virtual _u32 Write(int64 spos, const std::string &tw, bool *has_error = NULL);
	virtual _u32 Write(const char* buffer, _u32 bsize, bool *has_error=NULL);
	virtual _u32 Write(int64 spos, const char* buffer, _u32 bsize, bool *has_error = NULL);
	virtual _i64 Size(void);
	virtual _i64 RealSize(void);
	virtual bool PunchHole( _i64 spos, _i64 size );
	virtual bool Sync();
	
	bool Seek(_i64 offset);
	bool Read(char* buffer, size_t bsize, size_t &read);
	uint64 getSize(void);
	uint64 getRealSize(void);
	uint64 usedSize(void);
	char *getUID(void);
	unsigned int getTimestamp(void);
	std::string getFilename(void);

	bool has_sector(_i64 sector_size=-1);
	bool this_has_sector(_i64 sector_size=-1);

	bool has_block(bool use_parent=true);

	unsigned int getBlocksize();

	bool isOpen(void);

	void addVolumeOffset(_i64 offset);

	bool finish();

	VHDFile* getParent();

	bool isCompressed();

	bool trimUnused(_i64 fs_offset, _i64 trim_blocksize, ITrimCallback* trim_callback)
	{
		return true;
	}

	bool syncBitmap(_i64 fs_offset)
	{
		return true;
	}

	virtual bool makeFull(_i64 fs_offset, IVHDWriteCallback* write_callback);

private:

	bool check_if_compressed();

	bool write_header(bool diff);
	bool write_dynamicheader(char *parent_uid, unsigned int parent_timestamp, std::string parentfn);
	bool write_bat(void);
	bool write_footer(void);

	bool read_footer(void);
	bool process_footer(void);
	bool read_dynamicheader(void);
	bool read_bat(void);

	void init_bitmap(void);

	inline bool isBitmapSet(unsigned int offset);
	inline void setBitmapBit(unsigned int offset, bool v);
	void switchBitmap(uint64 new_offset);

	unsigned int calculate_chs(void);
	unsigned int calculate_checksum(const unsigned char * data, size_t dsize);

	void print_last_error();

	bool read_only;

	IFile* backing_file;
	IFile* file;
	CompressedFile* compressed_file;

	uint64 dstsize;

	VHDFile *parent;

	unsigned int blocksize;

	VHDFooter footer;
	VHDDynamicHeader dynamicheader;
	unsigned int *bat;
	unsigned int batsize;

	uint64 header_offset;
	uint64 dynamic_header_offset;
	uint64 bat_offset;

	uint64 nextblock_offset;

	unsigned char * bitmap;
	unsigned int bitmap_size;

	bool is_open;

	uint64 currblock;

	uint64 curr_offset;

	uint64 bitmap_offset;
	bool bitmap_dirty;

	bool fast_mode;

	_i64 volume_offset;

	bool finished;
};
