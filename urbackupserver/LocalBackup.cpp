#include "LocalBackup.h"
#include "dao/ServerBackupDao.h"
#include "server_settings.h"
#include "../urbackupcommon/os_functions.h"
#include "ClientMain.h"
#include "database.h"
#include "server.h"
#include <time.h>

extern std::string server_identity;

bool LocalBackup::doBackup()
{
	logid = ServerLogger::getLogId(clientid);
	db = nullptr;

	if (is_file_backup)
	{
		if (r_incremental)
		{
			status_id = ServerStatus::startProcess(clientname, sa_incr_file, details, logid, true);
		}
		else
		{
			status_id = ServerStatus::startProcess(clientname, sa_full_file, details, logid, true);
		}
	}
	else
	{
		if (r_incremental)
		{
			status_id = ServerStatus::startProcess(clientname, sa_incr_image, details, logid, true);
		}
		else
		{
			status_id = ServerStatus::startProcess(clientname, sa_full_image, details, logid, true);
		}
		ServerStatus::setProcessPcDone(clientname, status_id, 0);
	}

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	server_settings.reset(new ServerSettings(db, clientid));
	ServerBackupDao backup_dao(db);

	if (!constructBackupPath())
		return false;

	ServerBackupDao::SLastIncremental last_incremental = backup_dao.getLastIncrementalCompleteFileBackup(clientid, group);

	int incremental_ref = 0;
	int incremental = 0;

	if (last_incremental.exists)
	{
		incremental = last_incremental.incremental + 1;
		incremental_ref = last_incremental.id;
	}

	if (!backup_dao.newFileBackup(incremental, clientid, backuppath_single, 0, 0, group, incremental_ref, 1))
		return false;

	backupid = static_cast<int>(db->getLastInsertID());

	std::string pver = "3";

	std::string identity = client_main->getIdentity();

	std::string start_backup_cmd = identity + pver;

	if (!isIncrementalBackup())
	{
		start_backup_cmd += "START FULL BACKUP";
	}
	else
	{
		start_backup_cmd += "START BACKUP";
	}

	start_backup_cmd += " group=" + convert(group);
	if (!clientsubname.empty())
	{
		start_backup_cmd += "&clientsubname=" + EscapeParamString((clientsubname));
	}

	start_backup_cmd += "&running_jobs=" + convert(ServerStatus::numRunningJobs(clientname));

	/*if (resume)
	{
		start_backup_cmd += "&resume=";
		if (full)
			start_backup_cmd += "full";
		else
			start_backup_cmd += "incr";
	}*/

	if (BackupServer::useTreeHashing())
	{
		start_backup_cmd += "&sha=528";
	}
	else
	{
		start_backup_cmd += "&sha=512";
	}

	start_backup_cmd += "&with_permissions=1&with_scripts=1&with_orig_path=1&with_sequence=1&with_proper_symlinks=1";
	start_backup_cmd += "&status_id=" + convert(status_id);
	start_backup_cmd += "&log_id=" + convert(logid.first);
	start_backup_cmd += "&async=1";
	start_backup_cmd += "#token=" + server_token;

	ServerLogger::Log(logid, clientname + ": Connecting to startup backup...", LL_DEBUG);
	std::unique_ptr<IPipe> cc( client_main->getClientCommandConnection(server_settings.get(), 60000));
	if (!cc)
	{
		ServerLogger::Log(logid, "Connecting to client \"" + clientname + "\" to startup backup failed", LL_ERROR);
		has_early_error = true;
		return false;
	}

	CTCPStack tcpstack(client_main->isOnInternetConnection());

	if (tcpstack.Send(cc.get(), start_backup_cmd) != start_backup_cmd.size())
	{
		ServerLogger::Log(logid, "Requesting backup start failed", LL_ERROR);
		has_early_error = true;
		return false;
	}

	const unsigned int timeout_time = 10 * 60 * 1000;
	bool has_total_timeout;
	std::string async_id;
	int64 starttime = Server->getTimeMS();
	bool no_backup_dirs = false;

	while (!(has_total_timeout = Server->getTimeMS() - starttime > timeout_time))
	{
		if (ServerStatus::getProcess(clientname, status_id).stop)
		{
			ServerLogger::Log(logid, "Sever admin stopped backup during start", LL_WARNING);
			return false;
		}

		std::string ret;
		size_t rc = cc->Read(&ret, 60000);
		if (rc == 0)
		{
			ServerLogger::Log(logid, "Timeout while requesting backup start", LL_ERROR);
			has_timeout_error = true;
			should_backoff = false;
			return false;
		}
		tcpstack.AddData((char*)ret.c_str(), ret.size());

		bool has_error = false;
		while (tcpstack.getPacket(ret))
		{
			if (ret != "DONE")
			{
				if (async_id.empty()
					&& next(ret, 0, "ASYNC-"))
				{
					str_map params;
					ParseParamStrHttp(ret.substr(6), &params);

					async_id = params["async_id"];
					
					starttime = Server->getTimeMS();
					break;
				}
				else if (ret == "BUSY")
				{
					starttime = Server->getTimeMS();
				}
				else if (ret != "no backup dirs")
				{
					if (ret == "ERR")
					{
						client_main->forceReauthenticate();
					}
					ServerLogger::Log(logid, "Starting backup on \"" + clientname + "\" failed: " + ret, LL_ERROR);
					if (ret.find("Async indexing process not found") != std::string::npos)
					{
						ServerLogger::Log(logid, "Hint: The most likely explanation for this error is that the client was restarted/rebooted during indexing", LL_ERROR);
						should_backoff = false;
						has_timeout_error = true;
					}
					return false;
				}
				else
				{
					ServerLogger::Log(logid, "Starting backup on \"" + clientname + "\" failed: " + ret + ". Please add paths to backup on the client (via tray icon) or configure default paths to backup.", LL_ERROR);
					no_backup_dirs = true;
					break;
				}
			}
			else
			{
				return true;
			}
		}

		if (!async_id.empty())
		{
			break;
		}
	}

	if (has_total_timeout)
	{
		ServerLogger::Log(logid, "Starting backup of \"" + clientname + "\" failed - TIMEOUT(3)", LL_ERROR);

		has_timeout_error = true;
		should_backoff = false;
		return false;
	}

	return true;
}

bool LocalBackup::queryBackupFinished(int64 timeout_time, bool& finished)
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	server_settings.reset(new ServerSettings(db, clientid));

	finished = false;
	int64 starttime = Server->getTimeMS();
	std::unique_ptr<IPipe> cc;
	CTCPStack tcpstack(client_main->isOnInternetConnection());

	while (Server->getTimeMS() - starttime <= timeout_time)
	{
		if (cc.get() == NULL)
		{
			while (cc.get() == NULL
				&& Server->getTimeMS() - starttime <= timeout_time)
			{
				ServerLogger::Log(logid, clientname + ": Connecting for getting backup status...", LL_DEBUG);
				cc.reset(client_main->getClientCommandConnection(server_settings.get(), 60000));

				if (ServerStatus::getProcess(clientname, status_id).stop)
				{
					cc.reset();
					ServerLogger::Log(logid, "Sever admin stopped backup)", LL_WARNING);
					break;
				}

				if (cc.get() == NULL)
				{
					ServerLogger::Log(logid, clientname + ": Failed to connect to client. Retrying in 10s", LL_DEBUG);
					Server->wait(10000);
				}
			}

			if (cc.get() == NULL)
			{
				if (!ServerStatus::getProcess(clientname, status_id).stop)
				{
					ServerLogger::Log(logid, "Connecting to ClientService of \"" + clientname + "\" failed - CONNECT error while getting backup status(2)", LL_ERROR);
					has_timeout_error = true;
				}
				return false;
			}

			starttime = Server->getTimeMS();
			tcpstack.reset();
			std::string cmd = client_main->getIdentity() + "WAIT FOR INDEX async_id=" + async_id + "#token=" + server_token;
			tcpstack.Send(cc.get(), cmd);
		}

		std::string ret;
		size_t rc = cc->Read(&ret, 60000);
		if (rc == 0)
		{
			cc.reset();
			continue;
		}

		tcpstack.AddData((char*)ret.c_str(), ret.size());

		while (tcpstack.getPacket(ret))
		{
			if (ret == "DONE")
			{
				backup_result = true;
				finished = true;
				return true;
			}
			else if (ret == "BUSY")
			{
				finished = false;
				starttime = Server->getTimeMS();
				return true;
			}
			else
			{
				if (ret == "ERR")
				{
					client_main->forceReauthenticate();
					Server->wait(10000);
					cc.reset();
					break;
				}
				else
				{
					finished = true;
					backup_result = false;
					return true;
				}

				ServerLogger::Log(logid, "Backup status of \"" + clientname + "\" failed: " + ret, LL_ERROR);
				return false;
			}
		}
	}

	return false;
}

bool LocalBackup::finishBackup()
{
	if (!os_create_dir(backuppath + os_file_sep() + ".hashes"))
	{
		return false;
	}

	std::vector<std::string> del_files;

	if (!finishLocalBackup(del_files))
	{
		ServerLogger::Log(logid, "Error getting objects to be deleted", LL_ERROR);
		return false;
	}

	if (!del_files.empty())
	{
		Server->Log(clientname + ": Connecting to client...", LL_DEBUG);
		std::string identity = client_main->getIdentity();
		FileClient fc(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
			client_main->isOnInternetConnection(), client_main, nullptr);
		_u32 rc = client_main->getClientFilesrvConnection(&fc, server_settings.get(), 60000);
		if (rc != ERR_CONNECTED)
		{
			ServerLogger::Log(logid, "Error connecting to download objects to be deleted", LL_ERROR);
			return false;
		}

		fc.setProgressLogCallback(this);

		ServerLogger::Log(logid, clientname + ": Loading objects to be deleted", LL_INFO);

		size_t idx = 0;
		for (std::string del_file : del_files)
		{
			std::unique_ptr<IFsFile> del_f(Server->openFile(backuppath + os_file_sep()
				+ "del_objs_" + std::to_string(idx) + ".dat", MODE_WRITE));

			if (!del_f)
			{
				ServerLogger::Log(logid, "Error opening del_objs fn. "+os_last_error_str(), LL_ERROR);
				return false;
			}

			rc = fc.GetFile("urbackup/"+ del_file, del_f.get(), true, false, 0, false, 0);
			if (rc != ERR_SUCCESS)
			{
				ServerLogger::Log(logid, "Error objects to be deleted "+ del_file+" from " + clientname + 
					". Errorcode: " + fc.getErrorString(rc) + " (" + convert(rc) + ")", LL_ERROR);
				return false;
			}

			++idx;
		}
	}

	writestring("1", backuppath + os_file_sep()
		+ ".hashes" + os_file_sep() + ".local_backup_7267f182-4341-4cda-ada1-4c0c554322d8");

	const char* sync_fn = ".sync_f3a50226-f49a-4195-afef-c75b21781ae1";

	std::unique_ptr<IFile> syncf(Server->openFile(backuppath + os_file_sep() 
		+ ".hashes" + os_file_sep()+sync_fn, MODE_WRITE));

	if (!syncf)
		return false;

	if (!os_sync(backuppath))
		return false;

	if (!del_files.empty())
	{
		if (!finishLocalBackup(del_files))
		{
			ServerLogger::Log(logid, "Error finishing local backup on client", LL_ERROR);
			return false;
		}
	}

	return true;
}

bool LocalBackup::constructBackupPath()
{
	if (!createDirectoryForClient())
	{
		return false;
	}

	time_t tt = time(NULL);
#ifdef _WIN32
	tm lt;
	tm* t = &lt;
	localtime_s(t, &tt);
#else
	tm* t = localtime(&tt);
#endif
	char buffer[500];
	strftime(buffer, 500, "%y%m%d-%H%M", t);
	backuppath_single = (std::string)buffer;
	std::string backupfolder = server_settings->getSettings()->backupfolder;
	backuppath = backupfolder + os_file_sep() + clientname + os_file_sep() + backuppath_single;

	return os_create_dir(os_file_prefix(backuppath));
}

bool LocalBackup::finishLocalBackup(std::vector<std::string>& del_files)
{
	std::string params;

	if (del_files.empty())
	{
		params = "step=1";
	}
	else
	{
		params = "step=2";
		size_t idx = 0;
		for (std::string fn : del_files)
		{
			params += "&del_fn" + std::to_string(idx) + "=" + EscapeParamString(fn);
			++idx;
		}
	}

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	server_settings.reset(new ServerSettings(db, clientid));

	const int64 timeout_time = 60000;
	int64 starttime = Server->getTimeMS();
	std::unique_ptr<IPipe> cc;
	CTCPStack tcpstack(client_main->isOnInternetConnection());

	while (Server->getTimeMS() - starttime <= timeout_time)
	{
		if (cc.get() == NULL)
		{
			while (cc.get() == NULL
				&& Server->getTimeMS() - starttime <= timeout_time)
			{
				ServerLogger::Log(logid, clientname + ": Connecting for finishing backup...", LL_DEBUG);
				cc.reset(client_main->getClientCommandConnection(server_settings.get(), 60000));

				if (ServerStatus::getProcess(clientname, status_id).stop)
				{
					cc.reset();
					ServerLogger::Log(logid, "Sever admin stopped backup", LL_WARNING);
					break;
				}

				if (cc.get() == NULL)
				{
					ServerLogger::Log(logid, clientname + ": Failed to connect to client. Retrying in 10s", LL_DEBUG);
					Server->wait(10000);
				}
			}

			if (cc.get() == NULL)
			{
				if (!ServerStatus::getProcess(clientname, status_id).stop)
				{
					ServerLogger::Log(logid, "Connecting to ClientService of \"" + clientname + "\" failed - CONNECT error while getting backup status(2)", LL_ERROR);
					has_timeout_error = true;
				}
				return false;
			}

			starttime = Server->getTimeMS();
			tcpstack.reset();
			std::string cmd = client_main->getIdentity() + "FINISH LBACKUP?"+ params+"#token=" + server_token;
			tcpstack.Send(cc.get(), cmd);
		}

		std::string ret;
		size_t rc = cc->Read(&ret, 60000);
		if (rc == 0)
		{
			cc.reset();
			continue;
		}

		tcpstack.AddData((char*)ret.c_str(), ret.size());

		while (tcpstack.getPacket(ret))
		{
			if (ret == "ERR")
			{
				client_main->forceReauthenticate();
				Server->wait(10000);
				cc.reset();
				break;
			}
			else
			{
				str_map res;
				ParseParamStrHttp(ret, &res);

				if (res["ok"] != "1")
				{
					return false;
				}

				size_t idx = 0;
				while (res.find("del_fn" + std::to_string(idx))!=res.end())
				{
					del_files.push_back(res["del_fn" + std::to_string(idx)]);
					++idx;
				}

				return true;
			}
		}
	}

	return false;
}

void LocalBackup::log_progress(const std::string& fn, int64 total, int64 downloaded, int64 speed_bps)
{
	std::string fn_wo_token = fn;
	std::string share = getuntil("/", fn);
	if (!share.empty())
	{
		if (share.find("|") != std::string::npos)
		{
			fn_wo_token = getafter("|", fn);
		}
	}

	if (total > 0 && total != LLONG_MAX)
	{
		int pc_complete = 0;
		if (total > 0)
		{
			pc_complete = static_cast<int>((static_cast<float>(downloaded) / total) * 100.f);
		}
		ServerLogger::Log(logid, "Loading \"" + fn_wo_token + "\". " + convert(pc_complete) + "% finished " + PrettyPrintBytes(downloaded) + "/" + PrettyPrintBytes(total) + " at " + PrettyPrintSpeed(static_cast<size_t>(speed_bps)), LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(logid, "Loading \"" + fn_wo_token + "\". Loaded " + PrettyPrintBytes(downloaded) + " at " + PrettyPrintSpeed(static_cast<size_t>(speed_bps)), LL_DEBUG);
	}
}

