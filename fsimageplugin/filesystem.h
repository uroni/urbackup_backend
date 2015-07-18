#ifndef FILESYSTEM_H_5
#define FILESYSTEM_H_5

#include <string>

#include "../Interface/Types.h"
#include "../Interface/File.h"

#include "IFilesystem.h"

#include <memory>

class VHDFile;

namespace
{
	class ReadaheadThread;
}

class Filesystem : public IFilesystem
{
public:
	Filesystem(const std::wstring &pDev, bool read_ahead, bool background_priority);
	Filesystem(IFile *pDev, bool read_ahead, bool background_priority);
	virtual ~Filesystem();
	virtual int64 getBlocksize(void)=0;
	virtual int64 getSize(void)=0;
	virtual const unsigned char *getBitmap(void)=0;

	virtual bool hasBlock(int64 pBlock);
	virtual char* readBlock(int64 pBlock);
	std::vector<int64> readBlocks(int64 pStartBlock, unsigned int n,
		const std::vector<char*>& buffers, unsigned int buffer_offset);
	bool hasError(void);
	int64 calculateUsedSpace(void);
	virtual void releaseBuffer(char* buf);

	virtual void shutdownReadahead();

	virtual char* readBlockInt(int64 pBlock, bool use_readahead);

protected:
	bool readFromDev(char *buf, _u32 bsize);
	IFile *dev;

	bool has_error;

	bool own_dev;

	char* getBuffer();

	std::vector<char*> buffers;
	std::auto_ptr<IMutex> buffer_mutex;
	std::auto_ptr<ReadaheadThread> readahead_thread;
	THREADPOOL_TICKET readahead_thread_ticket;
};

#endif //FILESYSTEM_H_5