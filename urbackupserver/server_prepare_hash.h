#ifndef SERVER_PREPARE_HASH_H
#define SERVER_PREPARE_HASH_H

#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../Interface/Pipe.h"

#include "ChunkPatcher.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "server_log.h"




class BackupServerPrepareHash : public IThread, public IChunkPatcherCallback
{
public:
	BackupServerPrepareHash(IPipe *pPipe, IPipe *pOutput, int pClientid, logid_t logid);
	~BackupServerPrepareHash(void);

	void operator()(void);
	
	bool isWorking(void);

	void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed);

	bool hasError(void);

	static std::string hash_sha512(IFile *f);

private:
	
	std::string hash_with_patch(IFile *f, IFile *patch);

	IPipe *pipe;
	IPipe *output;

	int clientid;

	sha512_ctx ctx;

	ChunkPatcher chunk_patcher;
	
	volatile bool working;
	volatile bool has_error;

	logid_t logid;
};

#endif //SERVER_PREPARE_HASH_H