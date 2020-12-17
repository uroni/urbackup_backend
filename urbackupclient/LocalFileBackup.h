#pragma once
#include "../Interface/File.h"
#include "LocalBackup.h"
#include <vector>

class LocalFileBackup : public LocalBackup
{
public:
	LocalFileBackup(IBackupFileSystem* backup_files, int64 local_process_id, int64 server_log_id, int64 server_status_id, int64 backupid, std::string server_token, std::string server_identity, int facet_id)
		: LocalBackup(backup_files, local_process_id, server_log_id, server_status_id, backupid, std::move(server_token), std::move(server_identity), facet_id)
	{}

protected:
	_i64 getIncrementalSize(IFile* f, const std::vector<size_t>& diffs, bool& backup_with_components, bool all);

	bool hasChange(size_t line, const std::vector<size_t>& diffs);

	bool readFileOsMetadata(char* buf, size_t buf_avail, size_t& read_bytes);

	bool openFileHandle(const std::string& fn);

private:
};