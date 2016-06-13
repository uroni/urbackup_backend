#ifndef SERVER_PREPARE_HASH_H
#define SERVER_PREPARE_HASH_H

#include "../Interface/Thread.h"
#include "../Interface/File.h"
#include "../Interface/Pipe.h"

#include "ChunkPatcher.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "server_log.h"
#include "../urbackupcommon/ExtentIterator.h"
#include "../urbackupcommon/TreeHash.h"

const char HASH_FUNC_SHA512_NO_SPARSE = 0;
const char HASH_FUNC_SHA512 = 1;
const char HASH_FUNC_TREE = 2;

namespace
{
	std::string print_hash_func(const char hf)
	{
		switch (hf)
		{
		case HASH_FUNC_SHA512_NO_SPARSE: return "sha512-nosparse";
		case HASH_FUNC_SHA512: return "sha512-sparse";
		case HASH_FUNC_TREE: return "tree-sparse";
		default: return "unknown";
		}
	}
}

class BackupServerPrepareHash : public IThread, public IChunkPatcherCallback
{
public:
	BackupServerPrepareHash(IPipe *pPipe, IPipe *pOutput, int pClientid, logid_t logid, bool ignore_hash_mismatch);
	~BackupServerPrepareHash(void);

	void operator()(void);
	
	bool isWorking(void);

	void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed, bool* is_sparse);

	void next_sparse_extent_bytes(const char * buf, size_t bsize);

	bool hasError(void);

	class IHashProgressCallback
	{
	public:
		virtual void hash_progress(int64 curr) = 0;
	};


	static std::string calc_hash(IFsFile *f, std::string method);

	static bool hash_sha(IFile *f, IExtentIterator* extent_iterator, bool hash_with_sparse, IHashFunc& hashf, IHashProgressCallback* progress_callback=NULL);

private:
	
	bool hash_with_patch(IFile *f, IFile *patch, ExtentIterator* extent_iterator, bool hash_with_sparse);

	void readNextExtent();

	void addUnchangedHashes(int64 start, size_t size, bool* is_sparse);

	IPipe *pipe;
	IPipe *output;

	int clientid;

	IHashFunc* hashf;
	IFile* hashoutput_f;

	int64 file_pos;

	bool has_sparse_extents;

	ChunkPatcher chunk_patcher;
	
	volatile bool working;
	volatile bool has_error;

	logid_t logid;

	bool ignore_hash_mismatch;

};

#endif //SERVER_PREPARE_HASH_H