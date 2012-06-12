#include "../../Interface/Types.h"
#include "FileClient.h"
#include "../../md5.h"
#include "../../fileservplugin/chunk_settings.h"
#include <map>

class IFile;
class IPipe;
class CTCPStack;

const unsigned int c_max_queued_chunks=20;

enum EChunkedState
{
	CS_ID_FIRST,
	CS_ID_ACC,
	CS_BLOCK,
	CS_CHUNK
};

struct SChunkHashes
{
	char big_hash[big_hash_size];
	char small_hash[small_hash_size*(c_checkpoint_dist/c_small_hash_dist)];
};

class FileClientChunked
{
public:
	class ReconnectionCallback
	{
	public:
		virtual IPipe * new_fileclient_connection(void)=0;
	};

	FileClientChunked(IPipe *pipe, CTCPStack *stack, FileClientChunked::ReconnectionCallback *reconnection_callback);
	FileClientChunked(void);
	~FileClientChunked(void);

	_u32 GetFileChunked(std::string remotefn, IFile *file, IFile *chunkhashes, IFile *hashoutput);
	_u32 GetFilePatch(std::string remotefn, IFile *orig_file, IFile *patchfile, IFile *chunkhashes, IFile *hashoutput);

	_i64 getSize(void);

	bool hasError(void);

	void setDestroyPipe(bool b);

	_i64 getTransferredBytes(void);

private:

	_u32 GetFile(std::string remotefn);

	void State_First(void);
	void State_Acc(void);
	void State_Block(void);
	void State_Chunk(void);

	void Hash_finalize(_i64 curr_pos, const char *hash_from_client);
	void Hash_upto(_i64 chunk_start, bool &new_block);
	void Hash_nochange(_i64 curr_pos);

	void writeFileRepeat(IFile *f, const char *buf, size_t bsize);
	void writePatch(_i64 pos, unsigned int length, char *buf, bool last);
	void writePatchInt(_i64 pos, unsigned int length, char *buf);
	void writePatchSize(_i64 remote_fs);

	void invalidateLastPatches(void);

	bool Reconnect(void);

	std::string remote_filename;

	IFile *m_file;
	_i64 file_pos;
	IFile *m_patchfile;
	_i64 patchfile_pos;
	IFile *m_chunkhashes;
	IFile *m_hashoutput;
	IPipe *pipe;
	CTCPStack *stack;

	std::vector<_i64> last_chunk_patches;
	bool patch_mode;
	char patch_buf[c_chunk_size];
	unsigned int patch_buf_pos;
	_i64 patch_buf_start;

	_i64 next_chunk;
	_i64 num_chunks;
	_i64 remote_filesize;
	_i64 num_total_chunks;
	unsigned int starttime;
	unsigned int queued_chunks;

	EChunkedState state;
	char curr_id;
	unsigned int need_bytes;
	unsigned int total_need_bytes;
	char packet_buf[12];
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

	_i64 transferred_bytes;

	FileClientChunked::ReconnectionCallback *reconnection_callback;
};