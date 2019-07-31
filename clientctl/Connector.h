/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <string>
#include <vector>

typedef long long int int64;

struct SBackupDir
{
	std::string path;
	std::string name;
	int id;
	int group;
	std::string virtual_client;
	std::string flags;
};

struct SStatus
{
	std::string lastbackupdate;
	std::string status;
	std::string pcdone;
	bool pause;
};

struct SLogEntry
{
	int logid;
	std::string logtime;
};

struct SLogLine
{
	int loglevel;
	std::string msg;
};

struct SPathMap
{
	std::string source;
	std::string target;
};

struct SRunningProcess
{
	std::string action;
	int percent_done;
	int64 eta_ms;
	std::string details;
	int detail_pc;
	int64 process_id;
	int64 server_status_id;
	int64 total_bytes;
	int64 done_bytes;
	double speed_bpms;

	bool operator==(const SRunningProcess& other) const
	{
		return action == other.action
			&& percent_done == other.percent_done
			&& eta_ms / 1000 / 60 == other.eta_ms / 1000 / 60
			&& details == other.details
			&& detail_pc == other.detail_pc
			&& process_id == other.process_id
			&& server_status_id == other.server_status_id
			&& total_bytes == other.total_bytes
			&& done_bytes == other.done_bytes
			&& speed_bpms == other.speed_bpms;
	}
};

struct SFinishedProcess
{
	SFinishedProcess()
		:id(0), success(false)
	{
	}

	bool operator==(const SFinishedProcess& other) const
	{
		return id == other.id
			&& success == other.success;
	}

	int64 id;
	bool success;
};

struct SUrBackupServer
{
	bool internet_connection;
	std::string name;

	bool operator==(const SUrBackupServer& other) const
	{
		return internet_connection == other.internet_connection
			&& name == other.name;
	}
};

struct SStatusDetails
{
	bool ok;
	int64 last_backup_time;

	std::vector<SRunningProcess> running_processes;
	std::vector<SFinishedProcess> finished_processes;
	std::vector<SUrBackupServer> servers;
	int64 time_since_last_lan_connection;
	bool internet_connected;
	std::string internet_status;

	int capability_bits;

	bool operator==(const SStatusDetails& other) const
	{
		return ok == other.ok
			&& last_backup_time == other.last_backup_time
			&& running_processes == other.running_processes
			&& finished_processes == other.finished_processes
			&& servers == other.servers
			&& time_since_last_lan_connection / 1000 / 60 == other.time_since_last_lan_connection / 1000 / 60
			&& internet_connected == other.internet_connected
			&& internet_status == other.internet_status
			&& capability_bits == other.capability_bits;
	}
};

class Connector
{
public:
	static std::vector<SBackupDir> getSharedPaths(bool use_change_pw);
	static std::string getSharedPathsRaw();
	static bool saveSharedPaths(const std::vector<SBackupDir> &res);
	static std::string getStatusRaw();
	static std::string getStatusRawNoWait();
	static SStatus getStatus(void);
	static int startBackup(const std::string& virtual_client, bool full);
	static int startImage(const std::string& virtual_client, bool full);
	static bool updateSettings(const std::string &sdata, bool& no_perm);
	static std::vector<SLogEntry> getLogEntries(void);
	static std::vector<SLogLine> getLogdata(int logid, int loglevel);
	static bool setPause(bool b_pause);

	enum EAccessError
	{
		EAccessError_Ok,
		EAccessError_NoServer,
		EAccessError_NoTokens
	};

	static std::string getFileBackupsList(const std::string& virtual_client, EAccessError& access_error);
	static std::string getFileList(const std::string& path, int* backupid, const std::string& virtual_client, EAccessError& access_error);
	static std::string startRestore( const std::string& path, int backupid, const std::string& virtual_client,
		const std::vector<SPathMap>& map_paths, EAccessError& access_error, bool clean_other,
		bool ignore_other_fs, bool follow_symlinks);

	static std::string getStatusDetailsRaw();
	static SStatusDetails getStatusDetails();

	static std::string resetKeep(const std::string& virtualClient, const std::string& folderName, int tgroup);

	static void setPWFile(const std::string &pPWFile);
	static void setPWFileChange(const std::string &pPWFile);
	static void setClient(const std::string &pClient);

	static bool hasError(void);
	static bool isBusy(void);

private:
	static bool readTokens();

	static std::string getResponse(const std::string &cmd, const std::string &args, bool change_command);
	static std::string pw;
	static std::string pwfile;
	static std::string pwfile_change;
	static std::string client;
	static bool error;
	static bool busy;
	static std::string tokens;
};

#endif //CONNECTOR_H