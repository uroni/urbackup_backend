#pragma once
#include <string>
#include <memory>
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../urbackupcommon/os_functions.h"
#include "LocalFileBackup.h"
#include "../fileservplugin/IFileMetadataPipe.h"

class IBackupFileSystem;

class LocalIncrFileBackup : public LocalFileBackup
{
public:
	LocalIncrFileBackup(IBackupFileSystem* backup_files,
		int backupgroup, std::string clientsubname, int64 local_process_id, int64 server_log_id, int64 server_status_id,
		int64 backupid, std::string server_token, std::string server_identity, int facet_id)
		: backuppath(backuppath),
		backupgroup(backupgroup),
		clientsubname(clientsubname),
		LocalFileBackup(backup_files, local_process_id, server_log_id, server_status_id, backupid,
			std::move(server_token), std::move(server_identity), facet_id)
	{}

	bool prepareBackuppath();

	void operator()() {
		onStartBackup();
		run();
		onBackupFinish();
	}

	bool run();

private:

	bool writeOsMetadata(const std::string& sourcefn, int64 dest_start_offset, IFile* dest);

	std::string backuppath;
	int backupgroup;
	std::string clientsubname;
	std::auto_ptr<IFileMetadataPipe> file_metadata_pipe;
};