#ifndef FILESYSTEM_H_5
#define FILESYSTEM_H_5

#include <string>

#include "../Interface/Types.h"
#include "../Interface/File.h"

#include "IFilesystem.h"

class VHDFile;

class Filesystem : public IFilesystem
{
public:
	Filesystem(const std::wstring &pDev);
	Filesystem(IFile *pDev);
	virtual ~Filesystem();
	virtual int64 getBlocksize(void)=0;
	virtual int64 getSize(void)=0;
	virtual const unsigned char *getBitmap(void)=0;

	bool readBlock(int64 pBlock, char * buffer);
	std::vector<int64> readBlocks(int64 pStartBlock, unsigned int n, const std::vector<char*> buffers, unsigned int buffer_offset);
	bool hasError(void);
	int64 calculateUsedSpace(void);

protected:
	bool readFromDev(char *buf, _u32 bsize);
	IFile *dev;

	char *tmp_buf;
	unsigned int tmpbufsize;

	bool has_error;

	bool own_dev;
};

#endif //FILESYSTEM_H_5