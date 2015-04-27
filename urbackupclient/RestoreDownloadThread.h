#pragma once
#include "../Interface/Thread.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/fileclient/FileClientChunked.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../urbackupcommon/file_metadata.h"
#include <memory>

namespace
{
	enum EFileClient
	{
		EFileClient_Full,
		EFileClient_Chunked
	};

	struct SPatchDownloadFiles
	{
		IFile* orig_file;
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
			is_script(false)
		{
		}

		size_t id;
		std::wstring remotefn;
		std::wstring destfn;
		_i64 predicted_filesize;
		EFileClient fileclient;
		bool metadata_only;
		bool queued;
		EQueueAction action;
		SPatchDownloadFiles patch_dl_files;
		FileMetadata metadata;
		FileMetadata parent_metadata;
		bool is_script;
		bool is_dir;
	};
}

class RestoreDownloadThread : public IThread, public FileClient::QueueCallback, public FileClientChunked::QueueCallback
{
public:
	RestoreDownloadThread(FileClient& fc, FileClientChunked& fc_chunked, const std::string& client_token);

	void operator()();

	void addToQueueFull(size_t id, const std::wstring &remotefn, const std::wstring &destfn,
		_i64 predicted_filesize, const FileMetadata& metadata, const FileMetadata& parent_metadata, bool is_script, bool is_dir, bool at_front, bool metadata_only);

	void addToQueueChunked(size_t id, const std::wstring &remotefn, const std::wstring &destfn,
		_i64 predicted_filesize, const FileMetadata& metadata, const FileMetadata& parent_metadata, bool is_script, IFile* orig_file, IFile* chunkhashes);

	void queueSkip();

	bool load_file(SQueueItem todl);

	bool load_file_patch(SQueueItem todl);

	virtual std::string getQueuedFileFull( FileClient::MetadataQueue& metadata );

	virtual void unqueueFileFull( const std::string& fn );

	virtual void resetQueueFull();

	virtual bool getQueuedFileChunked( std::string& remotefn, IFile*& orig_file, IFile*& patchfile, IFile*& chunkhashes, IFile*& hashoutput, _i64& predicted_filesize );

	virtual void unqueueFileChunked( const std::string& remotefn );

	virtual void resetQueueChunked();

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
};