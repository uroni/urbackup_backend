#pragma once

#include "FileBackup.h"

class FullFileBackup : public FileBackup
{
public:
	FullFileBackup(ClientMain* client_main, int clientid, std::wstring clientname, LogAction log_action, int group, bool use_tmpfiles, std::wstring tmpfile_path, bool use_reflink, bool use_snapshots);

protected:
	virtual bool doFileBackup();


	SBackup FullFileBackup::getLastFullDurations();
};