#pragma once
#include "../Interface/Thread.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../urbackupcommon/file_metadata.h"
#include <memory>
#include <set>

namespace
{
	enum EFileClient
	{
		EFileClient_Full,
		EFileClient_Chunked
	};

	struct SPatchDownloadFiles
	{
		IFsFile* orig_file;
		IFile* chunkhashes;
	};

	enum EQueueAction
	{
		EQueueAction_Fileclient,
		EQueueAction_Quit,
		EQueueAction_Skip
	};

	struct SQueueItem
	{
		SQueueItem()
			: id(std::string::npos),
			fileclient(EFileClient_Full),
			queued(false),
			action(EQueueAction_Fileclient),
			is_script(false),
			folder_items(0)
		{
		}

		size_t id;
		std::string remotefn;
		std::string destfn;
		_i64 predicted_filesize;
		EFileClient fileclient;
		bool metadata_only;
		bool queued;
		EQueueAction action;
		SPatchDownloadFiles patch_dl_files;
		FileMetadata metadata;
		bool is_script;
		size_t folder_items;
	};
}

class RestoreDownloadThread : public IThread, public FileClient::QueueCallback, public FileClientChunked::QueueCallback
{
public:
	RestoreDownloadThread(FileClient& fc, FileClientChunked& fc_chunked, const std::string& client_token, str_map& metadata_path_mapping);

	void operator()();

	void addToQueueFull(size_t id, const std::string &remotefn, const std::string &destfn,
        _i64 predicted_filesize, const FileMetadata& metadata, bool is_script, bool metadata_only, size_t folder_items, IFsFile* orig_file);

	void addToQueueChunked(size_t id, const std::string &remotefn, const std::string &destfn,
		_i64 predicted_filesize, const FileMetadata& metadata, bool is_script, IFsFile* orig_file, IFile* chunkhashes);

	void queueSkip();

    void queueStop();

	bool load_file(SQueueItem todl);

	bool load_file_patch(SQueueItem todl);

	virtual std::string getQueuedFileFull( FileClient::MetadataQueue& metadata, size_t& folder_items, bool& finish_script, int64& file_id);

	virtual void unqueueFileFull( const std::string& fn, bool finish_script);

	virtual void resetQueueFull();

	virtual bool getQueuedFileChunked( std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFsFile*& hashoutput, _i64& predicted_filesize, int64& file_id, bool& is_script);

	virtual void unqueueFileChunked( const std::string& remotefn );

	virtual void resetQueueChunked();

    bool hasError();

	std::vector<std::pair<std::string, std::string> > getRenameQueue();

	bool isRenamedFile(const std::string& fn);

private:

	void sleepQueue(IScopedLock& lock);

	FileClient& fc;
	FileClientChunked& fc_chunked;

	std::deque<SQueueItem> dl_queue;
	size_t queue_size;

	bool all_downloads_ok;
	std::vector<size_t> download_nok_ids;

	std::auto_ptr<IMutex> mutex;
	std::auto_ptr<ICondition> cond;

	bool skipping;
	bool is_offline;

	const std::string& client_token;

	std::vector<std::pair<std::string, std::string> > rename_queue;
	str_map& metadata_path_mapping;
	std::set<std::string> renamed_files;
};
