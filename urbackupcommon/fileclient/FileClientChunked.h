#ifndef FILECLIENTCHUNKED_H
#define FILECLIENTCHUNKED_H

#include "../../Interface/Types.h"
#include "../../Interface/Mutex.h"
#include "FileClient.h"
#include "../../md5.h"
#include "../../fileservplugin/chunk_settings.h"
#include "../ExtentIterator.h"
#include <map>
#include <deque>

class IFile;
class IPipe;
class CTCPStack;

const unsigned int c_max_queued_chunks=1000;
const unsigned int c_queued_chunks_low=100;

enum EChunkedState
{
	CS_ID_FIRST,
	CS_ID_ACC,
	CS_BLOCK,
	CS_CHUNK,
	CS_SPARSE_EXTENTS
};

struct SChunkHashes
{
	char big_hash[big_hash_size];
	char small_hash[small_hash_size*(c_checkpoint_dist/c_small_hash_dist)];
};

int64 get_hashdata_size(int64 hashfilesize);

class FileClientChunked
{
public:
	class ReconnectionCallback
	{
	public:
		virtual IPipe * new_fileclient_connection(void)=0;
	};

	class NoFreeSpaceCallback
	{
	public:
		virtual bool handle_not_enough_space(const std::string &path)=0;
	};

	class QueueCallback
	{
	public:
		virtual bool getQueuedFileChunked(std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFsFile*& hashoutput, _i64& predicted_filesize, int64& file_id, bool& is_script) = 0;
		virtual void unqueueFileChunked(const std::string& remotefn) = 0;
		virtual void resetQueueChunked() = 0;
	};

	FileClientChunked(IPipe *pipe, bool del_pipe, CTCPStack *stack, FileClientChunked::ReconnectionCallback *reconnection_callback,
			FileClientChunked::NoFreeSpaceCallback *nofreespace_callback, std::string identity, FileClientChunked* prev);
	FileClientChunked(void);
	~FileClientChunked(void);

	_u32 GetFileChunked(std::string remotefn, IFile *file, IFile *chunkhashes, IFsFile *hashoutput, _i64& predicted_filesize, int64 file_id, bool is_script, IFile** sparse_extents_f);
	_u32 GetFilePatch(std::string remotefn, IFile *orig_file, IFile *patchfile, IFile *chunkhashes, IFsFile *hashoutput, _i64& predicted_filesize, int64 file_id, bool is_script, IFile** sparse_extents_f);

	bool hasError(void);

	void setDestroyPipe(bool b);

	_i64 getTransferredBytes(void);

	_i64 getRealTransferredBytes();

	_i64 getReceivedDataBytes(bool with_sparse);

	void resetReceivedDataBytes(bool with_sparse);

	void addThrottler(IPipeThrottler *throttler);

	IPipe *getPipe();

	void setReconnectionTimeout(unsigned int t);

	void setQueueCallback(FileClientChunked::QueueCallback* cb);

	void setProgressLogCallback(FileClient::ProgressLogCallback* cb);

	_u32 getErrorcode1();

	_u32 getErrorcode2();

	std::string getErrorcodeString();

	_u32 freeFile();

private:
	FileClientChunked(const FileClientChunked& other) {};
	void operator=(const FileClientChunked& other) {}

	void setQueueOnly(bool b);

	void setInitialBytes(const char* buf, size_t bsize);

	_u32 GetFile(std::string remotefn, _i64& filesize_out, int64 file_id, IFile** sparse_extents_f);

	_u32 handle_data(char* buf, size_t bsize, bool ignore_filesize, IFile** sparse_extents_f);

	void State_First(void);
	void State_Acc(bool ignore_filesize, IFile** sparse_extents_f);
	void State_Block(void);
	void State_Chunk(void);
	void State_SparseExtents(IFile** sparse_extents_f);

	void Hash_finalize(_i64 curr_pos, const char *hash_from_client);
	void Hash_upto(_i64 chunk_start, bool &new_block);
	void Hash_nochange(_i64 curr_pos);

	void writeFileRepeat(IFile *f, const char *buf, size_t bsize);
	void writePatch(_i64 pos, unsigned int length, char *buf, bool last);
	void writePatchInt(_i64 pos, unsigned int length, char *buf);
	void writePatchSize(_i64 remote_fs);

	void invalidateLastPatches(void);

	void calcTotalChunks();

	_u32 loadFileOutOfBand(IFile** sparse_extents_f);

	bool constructOutOfBandPipe();

	void requestOfbChunk(_i64 chunk_pos);

	_u32 loadChunkOutOfBand(_i64 chunk_pos);

	bool Reconnect(bool rerequest);

	void setPipe(IPipe* p);

	FileClientChunked* getNextFileClient();

	void clearFileClientQueue();

	unsigned int queuedChunks();
	void incrQueuedChunks();
	void decrQueuedChunks();
	void resetQueuedChunks();

	void addReceivedBytes(size_t bytes);

	void addSparseBytes(_i64 bytes);

	void addReceivedBlock(_i64 block_start);

	IPipe* ofbPipe();
	void setOfbPipe(IPipe* p);

	void logPendingChunks();

	void logTransferProgress();

	void adjustOutputFilesizeOnFailure( _i64& filesize_out);

	_u32 Flush(IPipe* fpipe);

	int getReconnectTries();

	int decrReconnectTries();

	void setReconnectTries(int tries);

	void setErrorCodes(_u32 ec1, _u32 ec2);

	std::string remote_filename;

	IFile *m_file;
	_i64 file_pos;
	IFile *m_patchfile;
	_i64 patchfile_pos;
	IFile *m_chunkhashes;
	IFsFile *m_hashoutput;
	IPipe *pipe;
	CTCPStack *stack;

	std::vector<_i64> last_chunk_patches;
	int64 last_patch_output_fsize;
	bool patch_mode;
	char patch_buf[c_chunk_size];
	unsigned int patch_buf_pos;
	_i64 patch_buf_start;

	_i64 next_chunk;
	_i64 num_chunks;
	_i64 remote_filesize;
	_i64 num_total_chunks;
	_i64 curr_output_fsize;
	int64 starttime;
	unsigned int queued_chunks;

	EChunkedState state;
	char curr_id;
	unsigned int need_bytes;
	unsigned int total_need_bytes;
	char packet_buf[24];
	size_t packet_buf_off;

	unsigned int whole_block_remaining;
	bool hash_for_whole_block;

	char *bufptr;
	size_t remaining_bufptr_bytes;
	size_t bufptr_bytes_done;

	//Chunk
	_i64 chunk_start;
	_i64 block_for_chunk_start;

	MD5 md5_hash;
	unsigned int adler_hash;
	unsigned int adler_remaining;

	unsigned int block_pos;

	_u32 retval;
	bool getfile_done;

	std::map<_i64, SChunkHashes> pending_chunks;

	bool has_error;
	bool destroy_pipe;

	_i64 real_transferred_bytes;
	_i64 transferred_bytes;

	FileClientChunked::ReconnectionCallback *reconnection_callback;
	FileClientChunked::NoFreeSpaceCallback *nofreespace_callback;

	std::vector<IPipeThrottler*> throttlers;

	unsigned int reconnection_timeout;

	std::string identity;

	_i64 received_data_bytes;
	_i64 sparse_bytes;

	IMutex* mutex;

	FileClientChunked* parent;
	bool did_queue_fc;
	std::deque<FileClientChunked*> queued_fcs;

	FileClientChunked::QueueCallback* queue_callback;
	bool queue_only;

	bool queue_next;

	std::vector<char> initial_bytes;

	IPipe* ofb_pipe;

	_i64 hashfilesize;

	int64 last_transferred_bytes;
	int64 last_progress_log;
	FileClient::ProgressLogCallback* progress_log_callback;

	_u32 errorcode1;
	_u32 errorcode2;
	
	bool reconnected;

	bool needs_flush;

	int64 curr_file_id;

	bool curr_is_script;

	std::auto_ptr<ExtentIterator> extent_iterator;
	IFsFile::SSparseExtent curr_sparse_extent;

	int reconnect_tries;
};

#endif //FILECLIENTCHUNKED_H