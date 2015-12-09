#include "../../Interface/Types.h"
#include "../filesystem.h"

class FSNTFSWIN : public Filesystem
{
public:
	FSNTFSWIN(const std::wstring &pDev, bool read_ahead, bool backgournd_priority);
	~FSNTFSWIN(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char * getBitmap(void);

	bool excludeFiles(const std::wstring& path, const std::wstring& fn_contains);
	bool excludeFile(const std::wstring& path);
	bool excludeSectors(int64 start, int64 count);
	bool excludeBlock(int64 block);

private:
	unsigned char *bitmap;

	unsigned int sectorsize;
	unsigned int clustersize;
	uint64 drivesize;
};