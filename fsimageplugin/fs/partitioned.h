#pragma once

#include "../filesystem.h"
#include <vector>

class FSPartitioned : public Filesystem
{
public:
	FSPartitioned(const std::string &pDev, 
		IFSImageFactory::EReadaheadMode read_ahead,
		bool background_priority,
		IFsNextBlockCallback* next_block_callback);
	~FSPartitioned(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char * getBitmap(void);

	virtual std::string getType() {
		return "partitioned";
	}

	virtual void logFileChanges(std::string volpath, int64 min_size, char* fc_bitmap) {
		return;
	}

private:

	void setHasBlock(int64 pBlock, bool v);

	unsigned char *bitmap;
	int64 drivesize;
	int64 blocksize;
};