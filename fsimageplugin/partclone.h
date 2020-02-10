#pragma once

#include "filesystem.h"


class Partclone : public Filesystem
{
public:
	Partclone(const std::string& pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback);
	Partclone(IFile* pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback);
	~Partclone();

	virtual void logFileChanges(std::string volpath, int64 min_size, char* fc_bitmap);
	virtual int64 getBlocksize(void);
	virtual int64 getSize(void);
	virtual const unsigned char* getBitmap(void);

	virtual std::string getType();

private:
	void init();

	unsigned char* bitmap;
	int64 block_size;
	int64 total_size;
	std::string fstype;
};
