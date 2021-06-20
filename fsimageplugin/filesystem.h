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

class Filesystem_ReadaheadThread;
class Filesystem;

enum ENextBlockState
{
	ENextBlockState_Queued,
	ENextBlockState_Ready,
	ENextBlockState_Error
};

const size_t fs_readahead_n_max_buffers=64;

struct SNextBlock;

class SBlockBuffer : public IFilesystem::IFsBuffer
{
public:
	virtual char* getBuf()
	{
		return buffer;
	}
	
	char* buffer;
	SNextBlock* block;
	ENextBlockState state;
};

struct SNextBlock
{	
	SBlockBuffer buffers[fs_readahead_n_max_buffers];
	size_t n_buffers;
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
	
	virtual IFsBuffer* readBlock(int64 pBlock, bool* p_has_error=NULL);
	std::vector<int64> readBlocks(int64 pStartBlock, unsigned int n,
		const std::vector<char*>& buffers, unsigned int buffer_offset);
	bool hasError(void);
	int64 calculateUsedSpace(void);
	virtual void releaseBuffer(IFsBuffer* buf);

	virtual void shutdownReadahead();

	virtual IFsBuffer* readBlockInt(int64 pBlock, bool use_readahead, bool* p_has_error);

	int64 nextBlockInt(int64 curr_block);

#ifdef _WIN32
	void overlappedIoCompletion(SNextBlock* block, DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, int64 offset);
#endif

	virtual int64 nextBlock(int64 curr_block);

	virtual void slowReadWarning(int64 passed_time_ms, int64 curr_block);

	virtual void waitingForBlockCallback(int64 curr_block);

	virtual int64 getOsErrorCode();

	void readaheadSetMaxNBuffers(int64 n_max_buffers);

	bool excludeFiles(const std::string& path, const std::string& fn_contains);
	bool excludeFile(const std::string& path);
	bool excludeSectors(int64 start, int64 count);
	bool excludeBlock(int64 block);

protected:
	bool readFromDev(char *buf, _u32 bsize);
	void initReadahead(IFSImageFactory::EReadaheadMode read_ahead, bool background_priority);
	bool queueOverlappedReads(bool force_queue);
	bool waitForCompletion(unsigned int wtimems);
	size_t usedNextBlocks();
	IFile *dev;
	
	void completionFreeBuffer(SBlockBuffer* block_buf);

	SBlockBuffer* completionGetBlock(int64 pBlock, bool* p_has_error);

	bool has_error;
	int64 errcode;

	bool own_dev;
	
	class SSimpleBuffer : public IFsBuffer
	{
	public:
		char* buf;
		virtual char* getBuf()
		{
			return buf;
		}
	};

	IFsBuffer* getBuffer();

	std::vector<SSimpleBuffer*> buffers;
	std::unique_ptr<IMutex> buffer_mutex;
	std::unique_ptr<Filesystem_ReadaheadThread> readahead_thread;
	THREADPOOL_TICKET readahead_thread_ticket;

	size_t num_uncompleted_blocks;
	int64 overlapped_next_block;
	std::vector<SNextBlock> next_blocks;
	std::map<int64, SBlockBuffer*> queued_next_blocks;
	std::stack<SNextBlock*> free_next_blocks;
	IFsNextBlockCallback* next_block_callback;

	IFSImageFactory::EReadaheadMode read_ahead_mode;

	int64 curr_fs_readahead_n_max_buffers;

#ifdef _WIN32
	HANDLE hVol;
#endif

};

#endif //FILESYSTEM_H_5