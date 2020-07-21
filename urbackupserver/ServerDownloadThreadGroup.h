#pragma once

#include "ServerDownloadThread.h"

class ServerDownloadThreadGroup
{
public:
	ServerDownloadThreadGroup(FileClient& fc, FileClientChunked* fc_chunked, const std::string& backuppath, const std::string& backuppath_hashes, const std::string& last_backuppath, const std::string& last_backuppath_complete, bool hashed_transfer, bool save_incomplete_file, int clientid,
		const std::string& clientname, const std::string& clientsubname,
		bool use_tmpfiles, const std::string& tmpfile_path, const std::string& server_token, bool use_reflink, int backupid, bool r_incremental, IPipe* hashpipe_prepare, ClientMain* client_main,
		int filesrv_protocol_version, int incremental_num, logid_t logid, bool with_hashes, const std::vector<std::string>& shares_without_snapshot,
		bool with_sparse_hashing, server::FileMetadataDownloadThread* file_metadata_download, bool sc_failure_fatal, size_t n_threads, ServerSettings* server_settings, bool intra_file_diffs,
		FilePathCorrections& filepath_corrections, MaxFileId& max_file_id);

	~ServerDownloadThreadGroup();

	void queueSkip();

	bool sleepQueue();

	bool isOffline();

	void addToQueueStartShadowcopy(const std::string& fn);

	void addToQueueStopShadowcopy(const std::string& fn);

	void queueStop();

	void addToQueueFull(size_t id, const std::string& fn, const std::string& short_fn, const std::string& curr_path, const std::string& os_path,
		_i64 predicted_filesize, const FileMetadata& metadata, bool is_script, bool metadata_only, size_t folder_items, const std::string& sha_dig,
		bool at_front_postpone_quitstop = false, unsigned int p_script_random = 0, std::string display_fn = std::string(), bool write_metadata = false);

	void addToQueueChunked(size_t id, const std::string& fn, const std::string& short_fn, const std::string& curr_path,
		const std::string& os_path, _i64 predicted_filesize, const FileMetadata& metadata, bool is_script, const std::string& sha_dig, unsigned int p_script_random = 0, std::string display_fn = std::string());

	size_t getNumIssues();

	bool getHasDiskError();

	bool hasTimeout();

	bool shouldBackoff();

	size_t getNumEmbeddedMetadataFiles();

	bool deleteTempFolder();

	bool isDownloadOk(size_t id);

	bool isDownloadPartial(size_t id);

	size_t getMaxOkId();

	bool join(int waitms);

private:
	ServerDownloadThread* getMinQueued();

	ServerDownloadThread::ActiveDlIds active_dls_ids;

	struct ServerDlThread
	{
		ServerDownloadThread* dl_thread;
		FileClient* fc;
		FileClientChunked* fc_chunked;
	};

	std::vector<ServerDlThread> dl_threads;
	std::vector<THREADPOOL_TICKET> tickets;
};