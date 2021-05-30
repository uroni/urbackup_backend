#pragma once
#include <string>
#include <memory>
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../urbackupcommon/os_functions.h"
#include "LocalFileBackup.h"

class IBackupFileSystem;

class LocalFullFileBackup : public LocalFileBackup
{
public:
	LocalFullFileBackup(IBackupFileSystem* backup_files,
		int backupgroup, std::string clientsubname, int64 local_process_id, int64 server_log_id, int64 server_status_id,
		int64 backupid, std::string server_token, std::string server_identity, int facet_id, size_t max_backups)
		: backuppath(backuppath),
		backupgroup(backupgroup),
		clientsubname(clientsubname),
		LocalFileBackup(backup_files, local_process_id, server_log_id, server_status_id, backupid,
			std::move(server_token), std::move(server_identity), facet_id, max_backups)
	{}

	bool prepareBackuppath();

	void operator()() {
		onStartBackup();
		backup_success = run();
		onBackupFinish(false);
	}

	bool run();


private:

	std::string backuppath;
	int backupgroup;
	std::string clientsubname;
};