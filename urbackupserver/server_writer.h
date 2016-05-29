#include "../Interface/Thread.h"
#include "../Interface/ThreadPool.h"

#include "../urbackupcommon/bufmgr.h"
#include "../fsimageplugin/IVHDFile.h"

#include <queue>
#include "server_log.h"

class IVHDFile;

struct BufferVHDItem
{
	char *buf;
	uint64 pos;
	unsigned int bsize;
};

struct FileBufferVHDItem
{
	char type;
	uint64 pos;
	unsigned int bsize;
};

class ServerFileBufferWriter;

class ServerVHDWriter : public IThread, public ITrimCallback, public IVHDWriteCallback
{
public:
	ServerVHDWriter(IVHDFile *pVHD, unsigned int blocksize, unsigned int nbufs, int pClientid, bool use_tmpfiles, int64 mbr_offset, IFile* hashfile, int64 vhd_blocksize, logid_t logid, int64 drivesize);
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
	void setHasError(bool b);

	void doFinish(void);

	IMutex * getVHDMutex(void);
	IVHDFile* getVHD(void);

	size_t getQueueSize(void);

	bool writeVHD(uint64 pos, char *buf, unsigned int bsize);
	void freeFile(IFile *buf);

	void writeRetry(IFile *f, char *buf, unsigned int bsize);

	void setDoTrim(bool b);
	void setDoMakeFull(bool b);

	void setMbrOffset(int64 offset);

	virtual void trimmed(_i64 trim_start, _i64 trim_stop);

	virtual bool emptyVHDBlock(int64 empty_start, int64 empty_end);

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
	volatile bool do_trim;
	volatile bool do_make_full;

	bool filebuffer;

	int64 mbr_offset;

	IFile* hashfile;

	int64 vhd_blocksize;

	int64 trimmed_bytes;

	logid_t logid;

	int64 drivesize;
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
