#pragma once
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/Thread.h"
#include "server_prepare_hash.h"
#include "../urbackupcommon/chunk_hasher.h"
#include "server_log.h"
#include <memory>

class BackupServerHash;

namespace server
{

namespace
{
	struct SFolderItem
	{
		SFolderItem()
			: created(0), modified(0),
			  accessed(0), folder_items(-1),
			  counted_items(0)
		{

		}

		std::string path;
		std::string os_path;
		int64 created;
		int64 modified;
		int64 accessed;
		int64 folder_items;
		int64 counted_items;
	};
}

class FileMetadataDownloadThread : public IThread
{
public:
	FileMetadataDownloadThread(FileClient* fc, const std::string& server_token, logid_t logid, int backupid, int clientid,
		bool use_tmpfiles, std::string tmpfile_path);
	FileMetadataDownloadThread(const std::string& server_token, std::string metadata_tmp_fn, int backupid, int clientid,
		bool use_tmpfiles, std::string tmpfile_path);
	~FileMetadataDownloadThread();

	virtual void operator()();

	bool applyMetadata(const std::string& backup_metadata_dir, const std::string& backup_dir,
		INotEnoughSpaceCallback *cb, BackupServerHash* local_hash, std::map<std::string, std::string>& filepath_corrections, bool is_complete,
		size_t& num_embedded_files);
	bool applyWindowsMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset, bool is_complete, int64& metadataf_pos);
    bool applyUnixMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset, bool is_complete, int64& metadataf_pos);

	bool getHasError();

	bool getHasFatalError();

	bool getHasTimeoutError();
	
	void shutdown();

	bool isDownloading();

	bool hasMetadataId(int64 id);

	int64 getTransferredBytes();

	void setProgressLogEnabled(bool b);

private:

	void copyForAnalysis(IFile* metadata_f);

	void addSingleFileItem(std::string dir_path);
	void addFolderItem(std::string path, const std::string& os_path, bool is_dir, int64 created, int64 modified, int64 accessed, int64 folder_items);

	std::vector<SFolderItem> saved_folder_items;

	std::auto_ptr<FileClient> fc;
	const std::string& server_token;

	std::vector<char> buffer;

	bool has_error;
	bool has_fatal_error;
	bool has_timeout_error;
	std::string metadata_tmp_fn;
	logid_t logid;

	int64 max_metadata_id;
	std::vector<int64> last_metadata_ids;

	bool dry_run;

	int backupid;
	int clientid;

	FileClient::ProgressLogCallback* orig_progress_log_callback;

	bool use_tmpfiles;
	std::string tmpfile_path;
};

int check_metadata();

} //namespace server

