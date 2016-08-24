
#include "../filesystem.h"

class FSUnknown : public Filesystem
{
public:
	FSUnknown(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback);
	~FSUnknown(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char * getBitmap(void);

private:
	unsigned char *bitmap;
	int64 drivesize;
};