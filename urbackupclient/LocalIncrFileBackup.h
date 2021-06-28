#pragma once
#include <string>
#include <memory>
#include <algorithm>
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../urbackupcommon/os_functions.h"
#include "LocalFileBackup.h"
#include "../fileservplugin/IFileMetadataPipe.h"
#include "../urbackupcommon/chunk_hasher.h"

class IBackupFileSystem;

class LocalIncrFileBackup : public LocalFileBackup, public IBuildChunkHashsUpdateCallback
{
public:
	LocalIncrFileBackup(int backupgroup, std::string clientsubname, int64 local_process_id, int64 server_log_id, int64 server_status_id,
		int64 backupid, std::string server_token, std::string server_identity, int facet_id, size_t max_backups,
		const std::string& dest_url, const std::string& dest_url_params,
		const str_map& dest_secret_params)
		: backuppath(backuppath),
		backupgroup(backupgroup),
		clientsubname(clientsubname),
		LocalFileBackup(true, local_process_id, server_log_id, server_status_id, backupid,
			std::move(server_token), std::move(server_identity), facet_id, max_backups,
			dest_url, dest_url_params, dest_secret_params)
	{}

	bool prepareBackuppath();

	void operator()() {
		if (!onStartBackup())
		{
			backup_success = false;
			return;
		}
		backup_success = run();
		onBackupFinish(false);
	}

	bool run();

	virtual void updateBchPc(int64 done, int64 total) override;

private:

	bool writeOsMetadata(const std::string& sourcefn, int64 dest_start_offset, IFile* dest);

	bool deleteFilesInSnapshot(IFile* curr_file_list, const std::vector<size_t>& deleted_ids,
		std::string snapshot_path, bool no_error, bool hash_dir);

	bool hasChange(size_t line, const std::vector<size_t>& diffs)
	{
		return std::binary_search(diffs.begin(), diffs.end(), line);
	}

	std::string backuppath;
	std::string last_backuppath;
	int backupgroup;
	std::string clientsubname;
	std::unique_ptr<IFileMetadataPipe> file_metadata_pipe;
	int64 laststatsupdate = 0;	
};