#pragma once

#include "FileBackup.h"
struct SFile;
class FileMetadata;

class IncrFileBackup : public FileBackup
{
public:
	IncrFileBackup(ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname, LogAction log_action,
		int group, bool use_tmpfiles, std::string tmpfile_path, bool use_reflink, bool use_snapshots, std::string server_token, std::string details);

protected:
	virtual bool doFileBackup();
	SBackup getLastIncremental(int group);
	bool deleteFilesInSnapshot(const std::string clientlist_fn, const std::vector<size_t> &deleted_ids, std::string snapshot_path, bool no_error);
	void addFileEntrySQLWithExisting( const std::string &fp, const std::string &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int incremental);
	void addSparseFileEntry( std::string curr_path, SFile &cf, int copy_file_entries_sparse_modulo, int incremental_num,
		std::string local_curr_os_path, size_t& num_readded_entries );
	void copyFile(const std::string& source, const std::string& dest,
		const std::string& hash_src, const std::string& hash_dest,
		const FileMetadata& metadata);
	bool doFullBackup();

	bool intra_file_diffs;

	IMutex* hash_existing_mutex;
};