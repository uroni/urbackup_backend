#pragma once
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/Thread.h"
#include "server_prepare_hash.h"
#include "../urbackupcommon/chunk_hasher.h"
#include "server_log.h"
#include <memory>

class BackupServerHash;
class FilePathCorrections;
class MaxFileId;

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
		INotEnoughSpaceCallback *cb, BackupServerHash* local_hash, FilePathCorrections& filepath_corrections,
		size_t& num_embedded_files, MaxFileId* max_file_id);
	bool applyWindowsMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset, int64& metadataf_pos);
    bool applyUnixMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset, int64& metadataf_pos);

	bool getHasError();

	bool getHasFatalError();

	bool getHasTimeoutError();
	
	void shutdown();

	bool isDownloading();

	bool isComplete();

	bool isFinished();

	bool hasMetadataId(int64 id);

	int64 getTransferredBytes();

	void setProgressLogEnabled(bool b);

	class FileMetadataApplyThread : public IThread
	{
	public:
		FileMetadataApplyThread(FileMetadataDownloadThread* fmdlt, const std::string& backup_metadata_dir, const std::string& backup_dir,
			INotEnoughSpaceCallback *cb, BackupServerHash* local_hash, FilePathCorrections& filepath_corrections,
			MaxFileId& max_file_id)
			: fmdlt(fmdlt), backup_metadata_dir(backup_metadata_dir), backup_dir(backup_dir),
			cb(cb), local_hash(local_hash), filepath_corrections(filepath_corrections),
			num_embedded_files(0), max_file_id(max_file_id), has_success(false) {}

		virtual void operator()();

		bool hasSuccess() { return has_success; }

		size_t getNumEmbeddedFiles() { return num_embedded_files; }
	private:
		FileMetadataDownloadThread* fmdlt;
		std::string backup_metadata_dir;
		std::string backup_dir;
		INotEnoughSpaceCallback *cb;
		BackupServerHash* local_hash;
		FilePathCorrections& filepath_corrections;
		size_t num_embedded_files;
		MaxFileId& max_file_id;
		bool has_success;
	};

private:

	void setComplete();
	void setFinished();

	void copyForAnalysis(IFile* metadata_f);

	void addSingleFileItem(std::string dir_path);
	void addFolderItem(std::string path, const std::string& os_path, bool is_dir, int64 created, int64 modified, int64 accessed, int64 folder_items);

	bool readRetry(IFile* metadata_f, char* buf, size_t bsize);

	std::vector<SFolderItem> saved_folder_items;

	std::auto_ptr<FileClient> fc;
	const std::string& server_token;

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
	std::vector<char> buffer;

	bool is_complete;
	bool is_finished;
	std::auto_ptr<IMutex> mutex;
	std::auto_ptr<ICondition> cond;
};

int check_metadata();

} //namespace server

