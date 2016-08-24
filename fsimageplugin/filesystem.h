#ifndef FILESYSTEM_H_5
#define FILESYSTEM_H_5

#ifdef _WIN32
#include <Windows.h>
#endif

#include <string>

#include "../Interface/Types.h"
#include "../Interface/File.h"

#include "IFilesystem.h"
#include "IFSImageFactory.h"

#include <memory>
#include <map>
#include <stack>

class VHDFile;

namespace
{
	class ReadaheadThread;
}

class Filesystem;

enum ENextBlockState
{
	ENextBlockState_Queued,
	ENextBlockState_Ready,
	ENextBlockState_Error
};

struct SNextBlock
{
	char* buffer;
	ENextBlockState state;
	Filesystem* fs;
#ifdef _WIN32
	OVERLAPPED ovl;
#endif
};

class Filesystem : public IFilesystem, public IFsNextBlockCallback
{
public:
	Filesystem(const std::string &pDev, IFSImageFactory::EReadaheadMode read_ahead, IFsNextBlockCallback* next_block_callback);
	Filesystem(IFile *pDev, IFsNextBlockCallback* next_block_callback);
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

	int64 nextBlockInt(int64 curr_block);

#ifdef _WIN32
	void overlappedIoCompletion(SNextBlock* block, DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, int64 offset);
#endif

	virtual int64 nextBlock(int64 curr_block);

protected:
	bool readFromDev(char *buf, _u32 bsize);
	void initReadahead(IFSImageFactory::EReadaheadMode read_ahead, bool background_priority);
	bool queueOverlappedReads(bool force_queue);
	bool waitForCompletion(unsigned int wtimems);
	size_t usedNextBlocks();
	IFile *dev;

	SNextBlock* completionGetBlock(int64 pBlock);

	bool has_error;

	bool own_dev;

	char* getBuffer();

	std::vector<char*> buffers;
	std::auto_ptr<IMutex> buffer_mutex;
	std::auto_ptr<ReadaheadThread> readahead_thread;
	THREADPOOL_TICKET readahead_thread_ticket;

	size_t num_uncompleted_blocks;
	int64 overlapped_next_block;
	std::vector<SNextBlock> next_blocks;
	std::map<int64, SNextBlock*> queued_next_blocks;
	std::stack<SNextBlock*> free_next_blocks;
	std::map<char*, SNextBlock*> used_next_blocks;
	IFsNextBlockCallback* next_block_callback;

	IFSImageFactory::EReadaheadMode read_ahead_mode;

#ifdef _WIN32
	HANDLE hVol;
#endif

};

#endif //FILESYSTEM_H_5