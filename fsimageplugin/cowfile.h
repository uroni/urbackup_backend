#pragma once

#include "IVHDFile.h"

class CowFile : public IVHDFile
{
public:
	CowFile(const std::wstring &fn, bool pRead_only, uint64 pDstsize);
	CowFile(const std::wstring &fn, const std::wstring &parent_fn, bool pRead_only);
	~CowFile();


	virtual bool Seek(_i64 offset);
	virtual bool Read(char* buffer, size_t bsize, size_t &read_bytes);
	virtual _u32 Write(const char *buffer, _u32 bsize, bool *has_error);
	virtual bool isOpen(void);
	virtual uint64 getSize(void);
	virtual uint64 usedSize(void);
	virtual std::string getFilename(void);
	virtual std::wstring getFilenameW(void);
	virtual bool has_sector(_i64 sector_size=-1);
	virtual bool this_has_sector(_i64 sector_size=-1);
	virtual unsigned int getBlocksize();
	virtual bool finish();
	virtual bool trimUnused(_i64 fs_offset, ITrimCallback* trim_callback);
	virtual bool syncBitmap(_i64 fs_offset);
	virtual bool makeFull(_i64 fs_offset, IVHDWriteCallback* write_callback) { return true; }


private:
	void setupBitmap();
	bool isBitmapSet(uint64 offset);
	void setBitmapBit(uint64 offset, bool v);
	void setBitmapRange(uint64 offset_start, uint64 offset_end, bool v);
	bool saveBitmap();
	bool loadBitmap(const std::string& bitmap_fn);
	bool setUnused(_i64 unused_start, _i64 unused_end);


	int fd;
	std::string filename;
	bool read_only;
	bool is_open;
	bool bitmap_dirty;

	uint64 filesize;
	_i64 curr_offset;

	std::vector<unsigned char> bitmap;

	bool finished;
};
