#ifndef SERVER_PREPARE_HASH_H
#define SERVER_PREPARE_HASH_H

#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../Interface/Pipe.h"

#include "ChunkPatcher.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "server_log.h"
#include "../urbackupcommon/ExtentIterator.h"



class BackupServerPrepareHash : public IThread, public IChunkPatcherCallback
{
public:
	BackupServerPrepareHash(IPipe *pPipe, IPipe *pOutput, int pClientid, logid_t logid);
	~BackupServerPrepareHash(void);

	void operator()(void);
	
	bool isWorking(void);

	void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed);

	void next_sparse_extent_bytes(const char * buf, size_t bsize);

	bool hasError(void);

	static std::string hash_sha(IFile *f, ExtentIterator* extent_iterator);

private:
	
	std::string hash_with_patch(IFile *f, IFile *patch, ExtentIterator* extent_iterator);

	void readNextExtent();

	IPipe *pipe;
	IPipe *output;

	int clientid;

	sha_def_ctx ctx;
	sha_def_ctx sparse_ctx;

	bool has_sparse_extents;

	ChunkPatcher chunk_patcher;
	
	volatile bool working;
	volatile bool has_error;

	logid_t logid;

};

#endif //SERVER_PREPARE_HASH_H