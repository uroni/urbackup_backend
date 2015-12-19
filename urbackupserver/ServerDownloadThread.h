#pragma once

#include <string>
#include <deque>
#include <algorithm>
#include <assert.h>

#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Pipe.h"
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "ClientMain.h"
#include "../urbackupcommon/file_metadata.h"


class FileClient;
class FileClientChunked;

namespace
{
	enum EFileClient
	{
		EFileClient_Full,
		EFileClient_Chunked,
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
		std::string hashpath;
		std::string filepath_old;
	};

	struct SQueueItem
	{
		SQueueItem()
			: id(std::string::npos),
			fileclient(EFileClient_Full),
			queued(false),
			action(EQueueAction_Fileclient),
			is_script(false),
			folder_items(0),
			script_end(false)
		{
		}

		size_t id;
		std::string fn;
		std::string short_fn;
		std::string curr_path;
		std::string os_path;
		_i64 predicted_filesize;
		EFileClient fileclient;
		bool queued;
		EQueueAction action;
		SPatchDownloadFiles patch_dl_files;
		FileMetadata metadata;
		bool is_script;
        bool metadata_only;
		size_t folder_items;
		bool script_end;
	};
	
	
	class IdRange
	{
	public:
		IdRange()
		 : min_id(std::string::npos), max_id(0), finalized(false)
		{}
		
		void add(size_t id)
		{
			max_id = (std::max)(id, max_id);
			min_id = (std::min)(id, min_id);
			ids.push_back(id);
			finalized=false;
		}
		
		void finalize()
		{
			std::sort(ids.begin(), ids.end());
			finalized=true;
		}
		
		bool hasId(size_t id)
		{
			assert(finalized);
			
			if(id>=min_id && id<=max_id)
			{
				return std::binary_search(ids.begin(), ids.end(), id);
			}
			else
			{
				return false;
			}
		}
		
	private:
		bool finalized;
		std::vector<size_t> ids;
		size_t min_id;
		size_t max_id;
	};
}



class ServerDownloadThread : public IThread, public FileClient::QueueCallback, public FileClientChunked::QueueCallback
{
public:
	ServerDownloadThread(FileClient& fc, FileClientChunked* fc_chunked, const std::string& backuppath, const std::string& backuppath_hashes, const std::string& last_backuppath, const std::string& last_backuppath_complete, bool hashed_transfer, bool save_incomplete_file, int clientid,
		const std::string& clientname,
		bool use_tmpfiles, const std::string& tmpfile_path, const std::string& server_token, bool use_reflink, int backupid, bool r_incremental, IPipe* hashpipe_prepare, ClientMain* client_main,
		int filesrv_protocol_version, int incremental_num, logid_t logid, bool with_hashes);

	~ServerDownloadThread();

	void operator()(void);

	void addToQueueFull(size_t id, const std::string &fn, const std::string &short_fn, const std::string &curr_path, const std::string &os_path,
        _i64 predicted_filesize, const FileMetadata& metadata, bool is_script, bool metadata_only, size_t folder_items, bool with_sleep_on_full=true, bool at_front_postpone_quitstop=false);

	void addToQueueChunked(size_t id, const std::string &fn, const std::string &short_fn, const std::string &curr_path,
		const std::string &os_path, _i64 predicted_filesize, const FileMetadata& metadata, bool is_script);

	void addToQueueStartShadowcopy(const std::string& fn);

	void addToQueueStopShadowcopy(const std::string& fn);

	void queueStop();

	void queueSkip();

	void queueScriptEnd(const std::string &fn);
	
	bool load_file(SQueueItem todl);
		
	bool load_file_patch(SQueueItem todl);

	bool logScriptOutput(std::string cfn, const SQueueItem &todl);

	bool isDownloadOk(size_t id);

	bool isDownloadPartial(size_t id);
	
	bool isAllDownloadsOk();

	size_t getMaxOkId();

	bool isOffline();

	void hashFile(std::string dstpath, std::string hashpath, IFile *fd, IFile *hashoutput, std::string old_file, int64 t_filesize,
		const FileMetadata& metadata, bool is_script);

	virtual bool getQueuedFileChunked(std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize, int64& file_id);

	virtual void resetQueueFull();

	virtual std::string getQueuedFileFull(FileClient::MetadataQueue& metadata, size_t& folder_items, bool& finish_script, int64& file_id);

	virtual void unqueueFileFull(const std::string& fn, bool finish_script);

	virtual void unqueueFileChunked(const std::string& remotefn);

	virtual void resetQueueChunked();

	bool hasTimeout();

private:
	void sleepQueue(IScopedLock& lock);

	std::string getDLPath(SQueueItem todl);

	SPatchDownloadFiles preparePatchDownloadFiles(SQueueItem todl, bool& full_dl);

	void start_shadowcopy(const std::string &path);

	void stop_shadowcopy(const std::string &path);

	
	bool link_or_copy_file(SQueueItem todl);

	size_t insertFullQueueEarliest(SQueueItem ni);

	void postponeQuitStop(size_t idx);


	FileClient& fc;
	FileClientChunked* fc_chunked;
	const std::string& backuppath;
	const std::string& backuppath_hashes;
	const std::string& last_backuppath;
	const std::string& last_backuppath_complete;
	bool hashed_transfer;
	bool save_incomplete_file;
	int clientid;
	const std::string& clientname;
	bool use_tmpfiles;
	const std::string& tmpfile_path;
	const std::string& server_token;
	bool use_reflink;
	int backupid;
	bool r_incremental;
	IPipe* hashpipe_prepare;
	ClientMain* client_main;
	int filesrv_protocol_version;
	bool skipping;
	int incremental_num;

	bool is_offline;
	bool has_timeout;

	std::deque<SQueueItem> dl_queue;
	size_t queue_size;

	bool all_downloads_ok;
	IdRange download_nok_ids;
	IdRange download_partial_ids;
	size_t max_ok_id;

	bool with_hashes;
	

	IMutex* mutex;
	ICondition* cond;

	logid_t logid;
};
