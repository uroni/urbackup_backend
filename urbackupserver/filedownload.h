#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "ChunkPatcher.h"
#include <string>
#include <memory>

class IFile;

struct FileDownloadQueueItemChunked
{
	std::string remotefn;
	IFsFile* orig_file;
	IFile* patchfile;
	IFile* chunkhashes;
	IFile* hashoutput;
	_i64 predicted_filesize;
	bool queued;
};

struct FileDownloadQueueItemFull
{
	std::string remotefn;
	_i64 predicted_filesize;
	bool queued;
};

enum SQueueStatus
{
	SQueueStatus_NoQueue,
	SQueueStatus_Queue,
	SQueueStatus_IsQueued
};

class FileDownload : public FileClientChunked::ReconnectionCallback, public IChunkPatcherCallback, public FileClientChunked::QueueCallback, public FileClient::QueueCallback
{
public:
	FileDownload(std::string servername, unsigned int tcpport);

	void filedownload(std::string remotefn, std::string dest, int method, int predicted_filesize, SQueueStatus queueStatus);

	void filedownload(std::string csvfile);

	virtual IPipe * new_fileclient_connection(void);
	virtual void next_chunk_patcher_bytes(const char *buf, size_t bsize, bool changed, bool* is_sparse);
	virtual void next_sparse_extent_bytes(const char *buf, size_t bsize);

	virtual bool getQueuedFileChunked(std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize, int64& file_id, bool& is_script);
	virtual void unqueueFileChunked(const std::string& remotefn);
	virtual void resetQueueChunked();

	virtual std::string getQueuedFileFull(FileClient::MetadataQueue& metadata, size_t& folder_items, bool& finish_script, int64& file_id);
	virtual void unqueueFileFull(const std::string& fn, bool finish_script);
	virtual void resetQueueFull();
private:

	bool copy_file_fd(IFile *fsrc, IFile *fdst);
	void cleanup_tmpfile(IFile *tmpfile);

	std::string m_servername;
	unsigned int m_tcpport;
	IFile *m_chunkpatchfile;
	int64 chunk_patch_pos;

	CTCPStack tcpstack;
	std::auto_ptr<FileClientChunked> fc_chunked;
	std::auto_ptr<FileClient> fc;

	std::vector<FileDownloadQueueItemChunked> dlqueueChunked;
	std::vector<FileDownloadQueueItemFull> dlqueueFull;
};