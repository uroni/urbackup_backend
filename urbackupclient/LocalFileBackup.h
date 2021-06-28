#pragma once
#include "../Interface/File.h"
#include "LocalBackup.h"
#include "../fileservplugin/IFileMetadataPipe.h"
#include "../urbackupcommon/file_metadata.h"
#include <vector>

class LocalFileBackup : public LocalBackup
{
public:
	LocalFileBackup(bool incr, int64 local_process_id, int64 server_log_id, 
		int64 server_status_id, int64 backupid, std::string server_token, std::string server_identity, 
		int facet_id, size_t max_backups, const std::string& dest_url, const std::string& dest_url_params,
		const str_map& dest_secret_params);

protected:
	_i64 getIncrementalSize(IFile* f, const std::vector<size_t>& diffs, bool& backup_with_components, bool all);

	bool hasChange(size_t line, const std::vector<size_t>& diffs);

	bool readFileOsMetadata(char* buf, size_t buf_avail, size_t& read_bytes);

	bool openFileHandle(const std::string& fn);

	void referenceShadowcopy(const std::string& name, const std::string& server_token, const std::string& clientsubname);

	void unreferenceShadowcopy(const std::string& name, const std::string& server_token, const std::string& clientsubname, int issues);

	std::string getBackupInternalDataDir() {
		return ".hashes\\d42992c8-f07f-46e0-bba3-2c44913f91aa";
	}

	std::string permissionsAllowAll();

	bool backupMetadata(IFsFile* metadataf, const std::string& sourcepath, FileMetadata* metadata);

	bool writeOsMetadata(const std::string& sourcefn, int64 dest_start_offset, IFile* dest);

	std::unique_ptr<IFileMetadataPipe> file_metadata_pipe;

private:
};