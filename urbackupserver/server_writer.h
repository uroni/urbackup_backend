#include "../Interface/Thread.h"
#include "../Interface/ThreadPool.h"

#include "../urbackupcommon/bufmgr.h"

#include <queue>

class IVHDFile;

struct BufferVHDItem
{
	char *buf;
	uint64 pos;
	unsigned int bsize;
};

struct FileBufferVHDItem
{
	uint64 pos;
	unsigned int bsize;
};

class ServerFileBufferWriter;

class ServerVHDWriter : public IThread
{
public:
	ServerVHDWriter(IVHDFile *pVHD, unsigned int blocksize, unsigned int nbufs, int pClientid);
	~ServerVHDWriter(void);

	void operator()(void);

	char *getBuffer(void);
	void writeBuffer(uint64 pos, char *buf, unsigned int bsize);

	void freeBuffer(char *buf);

	void checkFreeSpaceAndCleanup(void); 
	bool cleanupSpace(void);

	void doExit(void);
	void doExitNow(void);
	bool hasError(void);

	void doFinish(void);

	IMutex * getVHDMutex(void);
	IVHDFile* getVHD(void);

	size_t getQueueSize(void);

	void writeVHD(uint64 pos, char *buf, unsigned int bsize);
	void freeFile(IFile *buf);

	void writeRetry(IFile *f, char *buf, unsigned int bsize);

private:
	IVHDFile *vhd;

	CBufMgr2 *bufmgr;
	CFileBufMgr *filebuf;
	ServerFileBufferWriter *filebuf_writer;
	THREADPOOL_TICKET filebuf_writer_ticket;
	IFile *currfile;
	uint64 currfile_size;

	IMutex *mutex;
	IMutex *vhd_mutex;
	ICondition *cond;
	std::queue<BufferVHDItem> tqueue;

	unsigned int written;
	int clientid;

	volatile bool exit;
	volatile bool exit_now;
	volatile bool has_error;
	volatile bool finish;

	bool filebuffer;
};

class ServerFileBufferWriter : public IThread
{
public:
	ServerFileBufferWriter(ServerVHDWriter *pParent, unsigned int pBlocksize);
	~ServerFileBufferWriter(void);

	void operator()(void);

	void writeBuffer(IFile *buf);

	void doExit(void);
	void doExitNow(void);

private:
	ServerVHDWriter *parent;
	unsigned int blocksize;

	std::queue<IFile*> fb_queue;
	IMutex *mutex;
	ICondition *cond;

	volatile bool exit;
	volatile bool exit_now;

	unsigned int written;
};