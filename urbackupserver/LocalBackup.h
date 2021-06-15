#pragma once
#include "Backup.h"
#include "server_ping.h"
#include "../urbackupcommon/fileclient/FileClient.h"

class LocalBackup : public Backup, public FileClient::ProgressLogCallback
{
public:
	LocalBackup(ClientMain* client_main, int clientid, std::string clientname,
		std::string clientsubname, LogAction log_action, bool is_file_backup, bool is_incremental,
		std::string server_token, std::string details, bool scheduled, int group)
		: group(group),
	Backup(client_main, clientid, clientname, clientsubname, log_action, is_file_backup,
		is_incremental, server_token, details, scheduled) {}

	virtual bool doBackup() override;

	bool queryBackupFinished(int64 timeout_time, bool& finished);

	bool finishBackup();

	virtual void log_progress(const std::string& fn, int64 total, int64 downloaded, int64 speed_bps) override;

	int getBackupId() {
		return backupid;
	}

protected:
	bool constructBackupPath();

	bool finishLocalBackup(std::vector<std::string>& del_files);

	int backupid = 0;
	int group;
	std::string backuppath_single;
	std::string backuppath;
	std::string async_id;	
};
