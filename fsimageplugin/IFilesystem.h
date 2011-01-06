#include "../Interface/Object.h"

class IFilesystem : public IObject
{
public:
	virtual int64 getBlocksize(void)=0;
	virtual int64 getSize(void)=0;
	virtual const unsigned char *getBitmap(void)=0;

	virtual bool readBlock(int64 pBlock, char * buffer)=0;
	virtual std::vector<int64> readBlocks(int64 pStartBlock, unsigned int n, const std::vector<char*> buffers, unsigned int buffer_offset=0)=0;
	virtual bool hasError(void)=0;
	virtual int64 calculateUsedSpace(void)=0;
};