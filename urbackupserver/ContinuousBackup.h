#pragma once
#include "FileBackup.h"
#include "../Interface/Mutex.h"

class BackupServerContinuous;

class ContinuousBackup : public FileBackup
{
public:
	ContinuousBackup(ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname, LogAction log_action,
		int group, bool use_tmpfiles, std::string tmpfile_path, bool use_reflink, bool use_snapshots, std::string details);

	~ContinuousBackup();


protected:
	virtual bool doFileBackup();

	BackupServerContinuous* continuous_update;
	THREADPOOL_TICKET continuous_thread_ticket;
};