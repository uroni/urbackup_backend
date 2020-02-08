#include "../../Interface/Types.h"
#include "../filesystem.h"

class FSNTFSWIN : public Filesystem
{
public:
	FSNTFSWIN(const std::string& pDev, IFSImageFactory::EReadaheadMode read_ahead, bool background_priority, IFsNextBlockCallback* next_block_callback);
	~FSNTFSWIN(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char* getBitmap(void);

	virtual void logFileChanges(std::string volpath, int64 min_size, char* fc_bitmap);
	virtual void logFileBlocks(std::string volpath, const std::vector<int64>& blocks);

	virtual std::string getType();

private:
	void logFileChangesInt(const std::string& volpath, int64 min_size, char* fc_bitmap, int64& total_files, int64& total_changed_sectors);

	int64 countSectors(int64 start, int64 count, char* fc_bitmap);

	unsigned char* bitmap;

	unsigned int sectorsize;
	unsigned int clustersize;
	uint64 drivesize;
};