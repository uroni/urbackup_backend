#pragma once
#include <string>
#include <memory>
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "../urbackupcommon/os_functions.h"
#include "LocalFileBackup.h"
#include "../urbackupcommon/chunk_hasher.h"

class IBackupFileSystem;

class LocalFullFileBackup : public LocalFileBackup, public IBuildChunkHashsUpdateCallback
{
public:
	LocalFullFileBackup(int backupgroup, std::string clientsubname, int64 local_process_id, int64 server_log_id, int64 server_status_id,
		int64 backupid, std::string server_token, std::string server_identity, int facet_id, size_t max_backups,
		const std::string& dest_url, const std::string& dest_url_params, const str_map& dest_secret_params)
		: backuppath(backuppath),
		backupgroup(backupgroup),
		clientsubname(clientsubname),
		LocalFileBackup(false, local_process_id, server_log_id, server_status_id, backupid,
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

	std::string backuppath;
	int backupgroup;
	std::string clientsubname;
	int64 laststatsupdate = 0;
};