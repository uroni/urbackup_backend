#include "../../Interface/Types.h"
#include "../filesystem.h"

class FSNTFSWIN : public Filesystem
{
public:
	FSNTFSWIN(const std::wstring &pDev, bool read_ahead);
	~FSNTFSWIN(void);

	int64 getBlocksize(void);
	virtual int64 getSize(void);
	const unsigned char * getBitmap(void);

private:
	unsigned char *bitmap;

	unsigned int sectorsize;
	unsigned int clustersize;
	uint64 drivesize;
};