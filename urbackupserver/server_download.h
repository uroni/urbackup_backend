#pragma once

#include <string>
#include <deque>

#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "fileclient/FileClient.h"
#include "fileclient/FileClientChunked.h"
#include "server_get.h"
#include "file_metadata.h"


class FileClient;
class FileClientChunked;

enum EFileClient
{
	EFileClient_Full,
	EFileClient_Chunked
};

enum EQueueAction
{
	EQueueAction_Fileclient,
	EQueueAction_Quit,
	EQueueAction_StopShadowcopy,
	EQueueAction_StartShadowcopy,
	EQueueAction_Skip
};

struct SPatchDownloadFiles
{
	bool prepared;
	bool prepare_error;
	IFile* orig_file;
	IFile* patchfile;
	IFile* chunkhashes;
	bool delete_chunkhashes;
	IFile* hashoutput;
	std::wstring hashpath;
	std::wstring filepath_old;
};

struct SQueueItem
{
	SQueueItem()
		: id(std::string::npos),
		  fileclient(EFileClient_Full),
		  queued(false),
		  action(EQueueAction_Fileclient)
	{
	}

	size_t id;
	std::wstring fn;
	std::wstring short_fn;
	std::wstring curr_path;
	std::wstring os_path;
	_i64 predicted_filesize;
	EFileClient fileclient;
	bool queued;
	EQueueAction action;
	SPatchDownloadFiles patch_dl_files;
	FileMetadata metadata;
};

class ServerDownloadThread : public IThread, public FileClient::QueueCallback, public FileClientChunked::QueueCallback
{
public:
	ServerDownloadThread(FileClient& fc, FileClientChunked* fc_chunked, bool with_hashes, const std::wstring& backuppath, const std::wstring& backuppath_hashes, const std::wstring& last_backuppath, const std::wstring& last_backuppath_complete, bool hashed_transfer, bool save_incomplete_file, int clientid,
		const std::wstring& clientname,
		bool use_tmpfiles, const std::wstring& tmpfile_path, const std::string& server_token, bool use_reflink, int backupid, bool r_incremental, IPipe* hashpipe_prepare, BackupServerGet* server_get,
		int filesrv_protocol_version);

	~ServerDownloadThread();

	void operator()(void);

	void addToQueueFull(size_t id, const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path, _i64 predicted_filesize, const FileMetadata& metadata, bool at_front=false);

	void addToQueueChunked(size_t id, const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path, _i64 predicted_filesize, const FileMetadata& metadata);

	void addToQueueStartShadowcopy(const std::wstring& fn);

	void addToQueueStopShadowcopy(const std::wstring& fn);

	void queueStop(bool immediately);

	void queueSkip();
	
	bool load_file(SQueueItem todl);
		
	bool load_file_patch(SQueueItem todl);

	bool isDownloadOk(size_t id);

	bool isDownloadPartial(size_t id);
	
	bool isAllDownloadsOk();

	size_t getMaxOkId();

	bool isOffline();

	void hashFile(std::wstring dstpath, std::wstring hashpath, IFile *fd, IFile *hashoutput, std::string old_file, int64 t_filesize, const FileMetadata& metadata);

	virtual bool getQueuedFileChunked(std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize);

	virtual void resetQueueFull();

	virtual std::string getQueuedFileFull();

	virtual void unqueueFileFull(const std::string& fn);

	virtual void unqueueFileChunked(const std::string& remotefn);

	virtual void resetQueueChunked();

private:
	void sleepQueue(IScopedLock& lock);

	std::wstring getDLPath(SQueueItem todl);

	SPatchDownloadFiles preparePatchDownloadFiles(SQueueItem todl, bool& full_dl);

	void start_shadowcopy(const std::string &path);

	void stop_shadowcopy(const std::string &path);

	bool touch_file(SQueueItem todl);

	FileClient& fc;
	FileClientChunked* fc_chunked;
	bool with_hashes;
	const std::wstring& backuppath;
	const std::wstring& backuppath_hashes;
	const std::wstring& last_backuppath;
	const std::wstring& last_backuppath_complete;
	bool hashed_transfer;
	bool save_incomplete_file;
	int clientid;
	const std::wstring& clientname;
	bool use_tmpfiles;
	const std::wstring& tmpfile_path;
	const std::string& server_token;
	bool use_reflink;
	int backupid;
	bool r_incremental;
	IPipe* hashpipe_prepare;
	BackupServerGet* server_get;
	int filesrv_protocol_version;
	bool skipping;

	bool is_offline;

	std::deque<SQueueItem> dl_queue;
	size_t queue_size;

	bool all_downloads_ok;
	std::vector<size_t> download_nok_ids;
	std::vector<size_t> download_partial_ids;
	size_t max_ok_id;

	IMutex* mutex;
	ICondition* cond;
};