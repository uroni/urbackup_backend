
#include "../filesystem.h"

class FSUnknown : public Filesystem
{
public:
	FSUnknown(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback);
	~FSUnknown(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char * getBitmap(void);

	virtual void logFileChanges(std::string volpath, int64 min_size, char* fc_bitmap);

	virtual std::string getType();
private:
	unsigned char *bitmap;
	int64 drivesize;
	int64 blocksize;
};