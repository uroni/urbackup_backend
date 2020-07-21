/*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "FileBackup.h"
#include "ClientMain.h"
#include "server_status.h"
#include "server_log.h"
#include <assert.h>
#include "server_ping.h"
#include "database.h"
#include "../urbackupcommon/filelist_utils.h"
#include <algorithm>
#include "../urbackupcommon/os_functions.h"
#include "../urbackupcommon/file_metadata.h"
#include <sstream>
#include "create_files_index.h"
#include <time.h>
#include "snapshot_helper.h"
#include "server_dir_links.h"
#include "server_cleanup.h"
#include <stack>
#include <limits.h>
#include "FileMetadataDownloadThread.h"
#include "../utf8/utf8.h"
#include "server.h"
#include "../urbackupcommon/TreeHash.h"
#include "../common/data.h"
#include "PhashLoad.h"

#ifndef NAME_MAX
#define NAME_MAX _POSIX_NAME_MAX
#endif

const unsigned int full_backup_construct_timeout=4*60*60*1000;
extern std::string server_identity;

FileBackup::FileBackup( ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname, LogAction log_action,
	bool is_incremental, int group, bool use_tmpfiles, std::string tmpfile_path, bool use_reflink, bool use_snapshots, std::string server_token,
	std::string details, bool scheduled)
	:  Backup(client_main, clientid, clientname, clientsubname, log_action, true, is_incremental, server_token, details, scheduled),
	group(group), use_tmpfiles(use_tmpfiles), tmpfile_path(tmpfile_path), use_reflink(use_reflink), use_snapshots(use_snapshots),
	disk_error(false), with_hashes(false),
	backupid(-1), hashpipe(NULL), hashpipe_prepare(NULL), bsh(NULL), bsh_prepare(NULL),
	bsh_ticket(ILLEGAL_THREADPOOL_TICKET), bsh_prepare_ticket(ILLEGAL_THREADPOOL_TICKET), pingthread(NULL),
	pingthread_ticket(ILLEGAL_THREADPOOL_TICKET), cdp_path(false), metadata_download_thread_ticket(ILLEGAL_THREADPOOL_TICKET),
	last_speed_received_bytes(0), speed_set_time(0)
{
}


FileBackup::~FileBackup()
{
	destroyHashThreads();
}

ServerBackupDao::SDuration FileBackup::interpolateDurations(const std::vector<ServerBackupDao::SDuration>& durations)
{
	float duration=0;
	float indexing_time_ms=0;
	if(!durations.empty())
	{
		duration = static_cast<float>(durations[durations.size()-1].duration);
		indexing_time_ms = static_cast<float>(durations[durations.size()-1].indexing_time_ms);
	}

	if(durations.size()>1)
	{
		for(size_t i=durations.size()-1;i--;)
		{
			duration = 0.9f*duration + 0.1f*durations[i].duration;
			indexing_time_ms = 0.9f*indexing_time_ms + 0.1f*durations[i].indexing_time_ms;
		}
	}

	ServerBackupDao::SDuration ret = {
		static_cast<int>(indexing_time_ms+0.5f),
		static_cast<int>(duration+0.5f) };

		return ret;
}

bool FileBackup::getResult()
{
	return backup_result;
}

bool FileBackup::request_filelist_construct(bool full, bool resume, int group,
	bool with_token, bool& no_backup_dirs, bool& connect_fail, const std::string& clientsubname)
{
	if(server_settings->getSettings()->end_to_end_file_backup_verification)
	{
		client_main->sendClientMessage("ENABLE END TO END FILE BACKUP VERIFICATION", "OK", "Enabling end to end file backup verficiation on client failed.", 10000);
	}

	unsigned int timeout_time=full_backup_construct_timeout;
	if (client_main->getProtocolVersions().async_index_version > 0)
	{
		timeout_time = 10 * 60 * 1000;
	}
	else if(client_main->getProtocolVersions().file_protocol_version>=2)
	{
		timeout_time=120000;
	}

	CTCPStack tcpstack(client_main->isOnInternetConnection());

	ServerLogger::Log(logid, clientname+": Connecting for filelist...", LL_DEBUG);
	IPipe *cc=client_main->getClientCommandConnection(server_settings.get(), 60000);
	if(cc==NULL)
	{
		ServerLogger::Log(logid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error during filelist construction", LL_ERROR);
		connect_fail=true;
		return false;
	}

	std::string pver="";
	if(client_main->getProtocolVersions().file_protocol_version>=2) pver="2";
	if(client_main->getProtocolVersions().file_protocol_version_v2>=1) pver="3";

	std::string identity = client_main->getIdentity();

	std::string start_backup_cmd=identity+pver;

	if(full && !resume)
	{
		start_backup_cmd+="START FULL BACKUP";
	}
	else
	{
		start_backup_cmd+="START BACKUP";
	}

	if(client_main->getProtocolVersions().file_protocol_version_v2>=1)
	{
		start_backup_cmd+=" group="+convert(group);
		if(!clientsubname.empty())
		{
			start_backup_cmd+="&clientsubname="+EscapeParamString((clientsubname));
		}

		start_backup_cmd += "&running_jobs=" + convert(ServerStatus::numRunningJobs(clientname));
	}

	if(resume && client_main->getProtocolVersions().file_protocol_version_v2>=1)
	{
		start_backup_cmd+="&resume=";
		if(full)
			start_backup_cmd+="full";
		else
			start_backup_cmd+="incr";
	}

	if(client_main->getProtocolVersions().select_sha_version>0)
	{
		if (BackupServer::useTreeHashing())
		{
			start_backup_cmd += "&sha=528";
		}
		else
		{
			start_backup_cmd += "&sha=512";
		}
	}

	if (client_main->getProtocolVersions().file_protocol_version_v2 >= 1)
	{
		start_backup_cmd += "&with_permissions=1&with_scripts=1&with_orig_path=1&with_sequence=1&with_proper_symlinks=1";
		start_backup_cmd += "&status_id=" + convert(status_id);
	}

	bool phash = false;
	if (client_main->getProtocolVersions().phash_version > 0
		&& server_settings->getSettings()->internet_parallel_file_hashing)
	{
		start_backup_cmd += "&phash=1";
		phash = true;
	}

	bool async_index = false;
	if (client_main->getProtocolVersions().async_index_version > 0)
	{
		async_index = true;

		start_backup_cmd += "&async=1";
	}

	if(with_token)
	{
		start_backup_cmd+="#token="+server_token;
	}

	tcpstack.Send(cc, start_backup_cmd);

	std::string async_id;

	ServerLogger::Log(logid, clientname+": Waiting for filelist", LL_DEBUG);
	std::string ret;
	int64 total_starttime_s = Server->getTimeSeconds();
	int64 starttime=Server->getTimeMS();
	bool has_total_timeout;
	while( !(has_total_timeout=Server->getTimeMS()-starttime>timeout_time) )
	{
		if (ServerStatus::getProcess(clientname, status_id).stop)
		{
			ServerLogger::Log(logid, "Sever admin stopped backup during indexing", LL_WARNING);
			break;
		}

		if (cc == NULL)
		{
			if (async_id.empty())
			{
				ServerLogger::Log(logid, "Async id is empty", LL_ERROR);
				break;
			}

			while (cc==NULL
				&& Server->getTimeMS() - starttime <= timeout_time)
			{
				ServerLogger::Log(logid, clientname + ": Connecting for filelist (async)...", LL_DEBUG);
				cc = client_main->getClientCommandConnection(server_settings.get(), 10000);

				if (ServerStatus::getProcess(clientname, status_id).stop)
				{
					Server->destroy(cc);
					cc = NULL;
					ServerLogger::Log(logid, "Sever admin stopped backup during indexing (2)", LL_WARNING);
					break;
				}

				if (cc == NULL)
				{
					ServerLogger::Log(logid, clientname + ": Failed to connect to client. Retrying in 10s", LL_DEBUG);
					Server->wait(10000);
				}
			}

			if (cc == NULL)
			{
				if (!ServerStatus::getProcess(clientname, status_id).stop)
				{
					ServerLogger::Log(logid, "Connecting to ClientService of \"" + clientname + "\" failed - CONNECT error during filelist construction (2)", LL_ERROR);
					has_timeout_error = true;
					should_backoff = false;
				}
				return false;
			}

			tcpstack.reset();
			tcpstack.setAddChecksum(client_main->isOnInternetConnection());

			std::string cmd = identity + "WAIT FOR INDEX async_id=" + async_id;

			if (with_token)
			{
				cmd += "#token=" + server_token;
			}

			tcpstack.Send(cc, cmd);
		}

		size_t rc=cc->Read(&ret, 60000);
		if(rc==0)
		{		
			if (!async_id.empty())
			{
				Server->destroy(cc);
				cc = NULL;
				continue;
			}
			else if(client_main->getProtocolVersions().file_protocol_version<2
				&& Server->getTimeMS()-starttime<=20000 && with_token==true) //Compatibility with older clients
			{
				Server->destroy(cc);
				ServerLogger::Log(logid, clientname+": Trying old filelist request", LL_WARNING);
				return request_filelist_construct(full, resume, group, false, no_backup_dirs, connect_fail, clientsubname);
			}
			else
			{
				if(client_main->getProtocolVersions().file_protocol_version>=2 || pingthread->isTimeout() )
				{
					ServerLogger::Log(logid, "Constructing of filelist of \""+clientname+"\" failed - TIMEOUT(1)", LL_ERROR);
					has_timeout_error = true;
					should_backoff = false;
					break;
				}
				else
				{
					continue;
				}
			}
		}
		tcpstack.AddData((char*)ret.c_str(), ret.size());

		std::string ret;
		bool has_error = false;
		while(tcpstack.getPacket(ret))
		{
			if(ret!="DONE")
			{
				if (async_id.empty()
					&& next(ret, 0, "ASYNC-") )
				{
					str_map params;
					ParseParamStrHttp(ret.substr(6), &params);

					async_id = params["async_id"];
					Server->destroy(cc);
					cc = NULL;
					starttime = Server->getTimeMS();

					
				}
				else if(ret=="BUSY")
				{
					starttime=Server->getTimeMS();
				}
				else if (ret == "PHASH")
				{
					starttime = Server->getTimeMS();
					if (phash)
					{
						if (!startPhashDownloadThread(async_id))
						{
							ServerLogger::Log(logid, "Error starting parallel hash load", LL_ERROR);
							has_error = true;
							break;
						}
					}
				}
				else if(ret!="no backup dirs")
				{
					if (ret == "ERR")
					{
						client_main->forceReauthenticate();
					}
					logVssLogdata(Server->getTimeSeconds()-total_starttime_s);
					ServerLogger::Log(logid, "Constructing of filelist of \""+clientname+"\" failed: "+ret, LL_ERROR);
					if (ret.find("Async indexing process not found") != std::string::npos)
					{
						ServerLogger::Log(logid, "Hint: The most likely explanation for this error is that the client was restarted/rebooted during indexing", LL_ERROR);
						should_backoff = false;
						has_timeout_error = true;
					}
					has_error = true;
					break;
				}
				else
				{
					ServerLogger::Log(logid, "Constructing of filelist of \""+clientname+"\" failed: "+ret+". Please add paths to backup on the client (via tray icon) or configure default paths to backup.", LL_ERROR);
					no_backup_dirs=true;
					has_error = true;
					break;
				}				
			}
			else
			{
				logVssLogdata(Server->getTimeSeconds()-total_starttime_s);
				Server->destroy(cc);
				return true;
			}
		}

		if (has_error)
		{
			break;
		}
	}

	if (has_total_timeout)
	{
		ServerLogger::Log(logid, "Constructing of filelist of \"" + clientname + "\" failed - TIMEOUT(3)", LL_ERROR);

		has_timeout_error = true;
		should_backoff = false;
	}

	Server->destroy(cc);
	return false;
}

bool FileBackup::wait_for_async(const std::string& async_id, int64 timeout_time)
{
	int64 starttime = Server->getTimeMS();
	std::auto_ptr<IPipe> cc;
	CTCPStack tcpstack(client_main->isOnInternetConnection());

	while (Server->getTimeMS() - starttime <= timeout_time)
	{
		if (cc.get() == NULL)
		{
			while (cc.get() == NULL
				&& Server->getTimeMS() - starttime <= timeout_time)
			{
				ServerLogger::Log(logid, clientname + ": Connecting for async...", LL_DEBUG);
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
					ServerLogger::Log(logid, "Connecting to ClientService of \"" + clientname + "\" failed - CONNECT error during async (2)", LL_ERROR);
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
				return true;
			}
			else if (ret == "BUSY")
			{
				starttime = Server->getTimeMS();
			}
			else
			{
				if (ret == "ERR")
				{
					client_main->forceReauthenticate();
				}

				ServerLogger::Log(logid, "Async cmd of \"" + clientname + "\" failed: " + ret, LL_ERROR);
				return false;
			}
		}
	}

	ServerLogger::Log(logid, "Async cmd of \"" + clientname + "\" failed: Timeout", LL_ERROR);
	has_timeout_error = true;
	return false;
}

bool FileBackup::request_client_write_tokens()
{
	std::string ret = client_main->sendClientMessage("WRITE TOKENS ", "Error requesting client to write tokens", 10000, true, LL_WARNING);
	if (ret == "OK")
	{
		return true;
	}
	else if (next(ret, 0, "ASYNC-"))
	{
		str_map params;
		ParseParamStrHttp(ret.substr(6), &params);

		return wait_for_async(params["async_id"]);
	}
	else
	{
		ServerLogger::Log(logid, "Error requesting client to write tokens: " + ret, LL_WARNING);
	}

	return false;
}

bool FileBackup::hasEarlyError()
{
	return has_early_error;
}

void FileBackup::logVssLogdata(int64 vss_duration_s)
{
	std::string vsslogdata=client_main->sendClientMessageRetry("GET VSSLOG", "Getting index log data from client failed", 10000, 10, true, LL_WARNING);

	if(!vsslogdata.empty() && vsslogdata!="ERR")
	{
		std::vector<SLogEntry> entries = client_main->parseLogData(vss_duration_s, vsslogdata);

		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (entries[i].loglevel == LL_ERROR)
			{
				++num_issues;
			}

			parseSnapshotFailed(entries[i].data);
			ServerLogger::Log(logid, entries[i].data, entries[i].loglevel);
		}
	}
}

bool FileBackup::getTokenFile(FileClient &fc, bool hashed_transfer, bool request)
{
	if (request)
	{
		if (!request_client_write_tokens())
		{
			return false;
		}
	}

	bool has_token_file=true;
	
	IFsFile *tokens_file=Server->openFile(os_file_prefix(backuppath_hashes+os_file_sep()+".urbackup_tokens.properties"), MODE_WRITE);
	if(tokens_file==NULL)
	{
		ServerLogger::Log(logid, "Error opening "+backuppath_hashes+os_file_sep()+".urbackup_tokens.properties", LL_ERROR);
		return false;
	}
	_u32 rc=fc.GetFile("urbackup/tokens_"+server_token+".properties", tokens_file, hashed_transfer, false, 0, false, 0);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error getting tokens file of "+clientname+". Errorcode: "+fc.getErrorString(rc)+" ("+convert(rc)+")", LL_DEBUG);
		has_token_file=false;
	}
	Server->destroy(tokens_file);


	std::auto_ptr<ISettingsReader> urbackup_tokens(
		Server->createFileSettingsReader(os_file_prefix(backuppath_hashes+os_file_sep()+".urbackup_tokens.properties")));

	std::string access_key;
	std::string client_access_key = server_settings->getSettings()->client_access_key;
	if(urbackup_tokens->getValue("access_key", &access_key) &&
		!access_key.empty() &&
		access_key != client_access_key )
	{
		backup_dao->updateOrInsertSetting(clientid, "client_access_key", access_key);

		if(!client_access_key.empty())
		{
			backup_dao->deleteUsedAccessTokens(clientid);
		}

		ServerSettings::updateClient(clientid);
	}
	
	return has_token_file;
}

std::string FileBackup::clientlistName(int ref_backupid)
{
	return "urbackup/clientlist_b_" + convert(ref_backupid) + ".ub";
}

void FileBackup::createHashThreads(bool use_reflink, bool ignore_hash_mismatches)
{
	assert(bsh.empty());
	assert(bsh_prepare.empty());

	hashpipe=Server->createMemoryPipe();
	hashpipe_prepare=Server->createMemoryPipe();

	size_t h_cnt = server_settings->getSettings()->hash_threads;

	for (size_t i = 0; i < h_cnt; ++i)
	{
		BackupServerHash* curr_bsh = new BackupServerHash(hashpipe, clientid, use_snapshots, use_reflink, use_tmpfiles, logid, use_snapshots, max_file_id);
		BackupServerPrepareHash* curr_bsh_prepare = new BackupServerPrepareHash(hashpipe_prepare, hashpipe, clientid, logid, ignore_hash_mismatches);
		bsh.push_back(curr_bsh);
		bsh_prepare.push_back(curr_bsh_prepare);
		bsh_ticket.push_back(Server->getThreadPool()->execute(curr_bsh, "fbackup write" + convert(i)));
		bsh_prepare_ticket.push_back(Server->getThreadPool()->execute(curr_bsh_prepare, "fbackup hash" + convert(i)));
	}
}


void FileBackup::destroyHashThreads()
{
	if (!bsh_prepare.empty())
	{
		hashpipe_prepare->Write("exit");
		Server->getThreadPool()->waitFor(bsh_ticket);
		Server->getThreadPool()->waitFor(bsh_prepare_ticket);
		Server->destroy(hashpipe_prepare);
		Server->destroy(hashpipe);
	}

	bsh_ticket.clear();
	bsh_prepare_ticket.clear();
	hashpipe=NULL;
	hashpipe_prepare=NULL;
	bsh.clear();
	bsh_prepare.clear();
}

_i64 FileBackup::getIncrementalSize(IFile *f, const std::vector<size_t> &diffs, bool& backup_with_components, bool all)
{
	f->Seek(0);
	_i64 rsize=0;
	FileListParser list_parser;
	SFile cf;
	bool indirchange=false;
	size_t read;
	size_t line=0;
	char buffer[4096];
	int indir_currdepth=0;
	int depth=0;
	int indir_curr_depth=0;
	int changelevel=0;
	backup_with_components = false;

	if(all)
	{
		indirchange=true;
	}

	while( (read=f->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			bool b=list_parser.nextEntry(buffer[i], cf, NULL);
			if(b)
			{
				if(cf.isdir==true)
				{
					if (depth == 0
						&& cf.name == "windows_components_config"
						&& (client_main->getProtocolVersions().os_simple.empty()
							|| client_main->getProtocolVersions().os_simple=="windows") )
					{
						backup_with_components = true;
					}

					if(indirchange==false && hasChange(line, diffs) )
					{
						indirchange=true;
						changelevel=depth;
						indir_currdepth=0;
					}
					else if(indirchange==true)
					{
						if(cf.name!="..")
							++indir_currdepth;
						else
							--indir_currdepth;
					}

					if(cf.name==".." && indir_currdepth>0)
					{
						--indir_currdepth;
					}

					if(cf.name!="..")
					{
						++depth;
					}
					else
					{
						--depth;
						if(indirchange==true && depth==changelevel)
						{
							if(!all)
							{
								indirchange=false;
							}
						}
					}
				}
				else
				{
					if(indirchange==true || hasChange(line, diffs))
					{
						if(cf.size>0)
						{
							rsize+=cf.size;
						}						
					}
				}
				++line;
			}
		}

		if(read<4096)
			break;
	}

	return rsize;
}

void FileBackup::calculateDownloadSpeed(int64 ctime, FileClient & fc, FileClientChunked * fc_chunked)
{
	if (speed_set_time == 0)
	{
		speed_set_time = ctime;
	}

	if (ctime - speed_set_time>10000)
	{
		int64 received_data_bytes = fc.getTransferredBytes() + (fc_chunked != NULL ? fc_chunked->getTransferredBytes() : 0);

		int64 new_bytes = received_data_bytes - last_speed_received_bytes;
		int64 passed_time = ctime - speed_set_time;

		if (passed_time > 0)
		{
			speed_set_time = ctime;

			double speed_bpms = static_cast<double>(new_bytes) / passed_time;

			if (last_speed_received_bytes > 0)
			{
				ServerStatus::setProcessSpeed(clientname, status_id,
					speed_bpms);
			}

			last_speed_received_bytes = received_data_bytes;
		}
	}
}

void FileBackup::calculateEtaFileBackup( int64 &last_eta_update, int64& eta_set_time, int64 ctime, FileClient &fc, FileClientChunked* fc_chunked,
	int64 linked_bytes, int64 &last_eta_received_bytes, double &eta_estimated_speed, _i64 files_size )
{
	last_eta_update=ctime;

	int64 received_data_bytes = fc.getReceivedDataBytes(true) + (fc_chunked?fc_chunked->getReceivedDataBytes(true):0) + linked_bytes;

	int64 new_bytes =  received_data_bytes - last_eta_received_bytes;
	int64 passed_time = Server->getTimeMS() - eta_set_time;

	if (passed_time > 0)
	{
		eta_set_time = Server->getTimeMS();

		double speed_bpms = static_cast<double>(new_bytes) / passed_time;

		if (eta_estimated_speed == 0)
		{
			eta_estimated_speed = speed_bpms;
		}
		else
		{
			eta_estimated_speed = eta_estimated_speed*0.9 + speed_bpms*0.1;
		}

		if (last_eta_received_bytes > 0 && eta_estimated_speed > 0)
		{
			ServerStatus::setProcessEta(clientname, status_id,
				static_cast<int64>((files_size - received_data_bytes) / eta_estimated_speed + 0.5),
				eta_set_time);
		}

		last_eta_received_bytes = received_data_bytes;
	}
}

bool FileBackup::doBackup()
{
	if(!client_main->handle_not_enough_space(""))
	{
		return false;
	}

	if( client_main->isOnInternetConnection() )
	{
		if( server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash")
		{
			with_hashes=true;
		}
	}
	
	if( server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash")
	{
		with_hashes=true;
	}

	ServerBackupDao::CondInt c_with_hashes = backup_dao->getClientWithHashes(clientid);
	if (c_with_hashes.exists
		&& c_with_hashes.value != 0)
	{
		with_hashes = true;
	}
	else if (with_hashes)
	{
		backup_dao->updateClientWithHashes(1, clientid);
	}

	if(!fileindex.get())
	{
		fileindex.reset(create_lmdb_files_index());
	}

	if(!cdp_path)
	{
		std::string errmsg;
		if(!constructBackupPath(use_snapshots, !r_incremental, errmsg))
		{
			errmsg = trim(errmsg);
            ServerLogger::Log(logid, "Cannot create directory "+backuppath+" for backup (server error). "
				+ (errmsg.empty() ? os_last_error_str() : errmsg), LL_ERROR);
			return false;
		}
	}
	else
	{
		if(!constructBackupPathCdp())
		{
            ServerLogger::Log(logid, "Cannot create directory "+backuppath+" for backup (server error). "+os_last_error_str(), LL_ERROR);
			return false;
		}
	}

	pingthread =new ServerPingThread(client_main, clientname, status_id, client_main->getProtocolVersions().eta_version>0, server_token);
	pingthread_ticket=Server->getThreadPool()->execute(pingthread, "client ping");

	local_hash.reset(new BackupServerHash(NULL, clientid, use_snapshots, use_reflink, use_tmpfiles, logid, use_snapshots, max_file_id));
	local_hash->setupDatabase();

	createHashThreads(use_reflink, server_settings->getSettings()->ignore_disk_errors);
	

	bool backup_result = doFileBackup();

	if(pingthread!=NULL)
	{
		pingthread->setStop(true);
		Server->getThreadPool()->waitFor(pingthread_ticket);
		pingthread=NULL;
	}	

	local_hash->deinitDatabase();

	stopPhashDownloadThread(filelist_async_id);

	if(disk_error)
	{
		ServerLogger::Log(logid, "FATAL: Backup failed because of disk problems (see previous messages)", LL_ERROR);
		client_main->sendMailToAdmins("Fatal error occurred during backup", ServerLogger::getWarningLevelTextLogdata(logid));
	}

	if((!has_early_error && !backup_result) || disk_error)
	{
		backup_result = false;
	}
	else if(has_early_error)
	{
		ServerLogger::Log(logid, "Backup had an early error. Deleting partial backup.", LL_ERROR);

		deleteBackup();
	}
	else
	{
		backup_dao->updateClientLastFileBackup(backupid, static_cast<int>(num_issues), clientid);
		backup_dao->updateFileBackupSetComplete(backupid);
	}


	return backup_result;
}

bool FileBackup::hasChange(size_t line, const std::vector<size_t> &diffs)
{
	return std::binary_search(diffs.begin(), diffs.end(), line);
}

std::string FileBackup::fixFilenameForOS(std::string fn, std::set<std::string>& samedir_filenames, const std::string& curr_path, bool log_warnings, logid_t logid, FilePathCorrections& filepath_corrections)
{
	std::string orig_fn = fn;

	bool append_hash = false;
#ifdef _WIN32
	std::string disallowed_chars = "\\:*?\"<>|/";
	for(char ch=1;ch<=31;++ch)
	{
		disallowed_chars+=ch;
	}

	if(fn=="CON" || fn=="PRN" || fn=="AUX" || fn=="NUL" || fn=="COM1" || fn=="COM2" || fn=="COM3" ||
		fn=="COM4" || fn=="COM5" || fn=="COM6" || fn=="COM7" || fn=="COM8" || fn=="COM9" || fn=="LPT1" ||
		fn=="LPT2" || fn=="LPT3" || fn=="LPT4" || fn=="LPT5" || fn=="LPT6" || fn=="LPT7" || fn=="LPT8" || fn=="LPT9")
	{
		ServerLogger::Log(logid, "Filename \""+fn+"\" not allowed on Windows. Prefixing and appending hash.", LL_WARNING);
		fn = "_" + fn;
		append_hash =true;
	}

	if(next(fn, 0, "CON.") || next(fn, 0, "PRN.") || next(fn, 0, "AUX.") || next(fn, 0, "NUL.") || next(fn, 0, "COM1.") || next(fn, 0, "COM2.") || next(fn, 0, "COM3.") ||
		next(fn, 0, "COM4.") || next(fn, 0, "COM5.") || next(fn, 0, "COM6.") || next(fn, 0, "COM7.") || next(fn, 0, "COM8.") || next(fn, 0, "COM9.") || next(fn, 0, "LPT1.") ||
		next(fn, 0, "LPT2.") || next(fn, 0, "LPT3.") || next(fn, 0, "LPT4.") || next(fn, 0, "LPT5.") || next(fn, 0, "LPT6.") || next(fn, 0, "LPT7.") || next(fn, 0, "LPT8.") || next(fn, 0, "LPT9.") )
	{
		if (log_warnings)
		{
			ServerLogger::Log(logid, "Filename \"" + fn + "\" not allowed on Windows. Prefixing and appending hash.", LL_WARNING);
		}
		fn = "_" + fn;
		append_hash =true;
	}
#else
	std::string disallowed_chars = "/";
#endif

	for(size_t i=0;i<disallowed_chars.size();++i)
	{
		char ch = disallowed_chars[i];
		if(fn.find(ch)!=std::string::npos)
		{
			if (log_warnings)
			{
				ServerLogger::Log(logid, "Filename \"" + fn + "\" contains '" + std::string(1, ch) + "' which the operating system does not allow in paths. Replacing '" + std::string(1, ch) + "' with '_' and appending hash.", LL_WARNING);
			}
			fn = ReplaceChar(fn, ch, '_');
			append_hash = true;
		}
	}

#ifdef _WIN32
	std::vector<utf8::uint16_t> tmp;
	bool unicode_err = false;
	try
	{
		utf8::utf8to16(fn.begin(), fn.end(), back_inserter(tmp));
	}
	catch (...)
	{
		unicode_err = true;
	}

	if (unicode_err)
	{
		if (log_warnings)
		{
			ServerLogger::Log(logid, "Filename \"" + fn + "\" has encoding problems (assuming UTF-8 encoding). Mangling filename.", LL_WARNING);
		}

		fn.clear();
		try
		{
			utf8::utf16to8(tmp.begin(), tmp.end(), back_inserter(fn));
		}
		catch (...)
		{
		}
		append_hash = true;
	}

	size_t ntfs_name_max = 255;

	if (append_hash)
	{
		ntfs_name_max -= 11;
	}

	if (tmp.size() > ntfs_name_max)
	{
		if (!append_hash)
		{
			ntfs_name_max -= 11;
		}
		if (log_warnings)
		{
			ServerLogger::Log(logid, "Filename \"" + fn + "\" too long. Shortening it and appending hash.", LL_WARNING);
		}
		tmp.resize(ntfs_name_max);
		fn.clear();
		try
		{
			utf8::utf16to8(tmp.begin(), tmp.end(), back_inserter(fn));
		}
		catch (...)
		{
		}
		append_hash = true;
	}
#else
	size_t name_max = NAME_MAX-1;

	if (append_hash)
	{
		name_max -= 11;
	}

	if (fn.size() > name_max)
	{
		if (!append_hash)
		{
			name_max -= 11;
		}
		if (log_warnings)
		{
			ServerLogger::Log(logid, "Filename \"" + fn + "\" too long. Shortening it and appending hash.", LL_WARNING);
		}
		fn.resize(name_max);
		append_hash = true;
	}
#endif

	if(append_hash)
	{
		std::string hex_md5 = Server->GenerateHexMD5(orig_fn);
		size_t dot_pos = fn.find_last_of('.');
		if (dot_pos != std::string::npos)
		{
			fn.insert(dot_pos, "-" + hex_md5.substr(0, 10));
		}
		else
		{
			fn = fn + "-" + hex_md5.substr(0, 10);
		}
	}

#ifdef _WIN32
	size_t idx=0;
	std::string base=fn;
	while(samedir_filenames.find(strlower(fn))!=samedir_filenames.end())
	{
		fn = base + "_" + convert(idx);
		++idx;
	}

	samedir_filenames.insert(strlower(fn));
#endif

	if(fn!= orig_fn)
	{
		if(curr_path.empty())
		{
			filepath_corrections.add(orig_fn, fn);
		}
		else
		{
			filepath_corrections.add(curr_path + "/" + orig_fn, fn);
		}
	}

	return fn;
}

void FileBackup::log_progress(const std::string & fn, int64 total, int64 downloaded, int64 speed_bps)
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

	if (total>0 && total != LLONG_MAX)
	{
		int pc_complete = 0;
		if (total>0)
		{
			pc_complete = static_cast<int>((static_cast<float>(downloaded) / total)*100.f);
		}
		ServerLogger::Log(logid, "Loading \"" + fn_wo_token + "\". " + convert(pc_complete) + "% finished " + PrettyPrintBytes(downloaded) + "/" + PrettyPrintBytes(total) + " at " + PrettyPrintSpeed(static_cast<size_t>(speed_bps)), LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(logid, "Loading \"" + fn_wo_token + "\". Loaded " + PrettyPrintBytes(downloaded) + " at " + PrettyPrintSpeed(static_cast<size_t>(speed_bps)), LL_DEBUG);
	}
}

bool FileBackup::create_hardlink(const std::string & linkname, const std::string & fname, bool use_ioref, bool * too_many_links, bool* copy)
{
	if (use_ioref && BackupServer::isReflinkCopy())
	{
		if (copy != NULL) *copy = true;
		return copy_file(fname, linkname);
	}

	return os_create_hardlink(linkname, fname, use_ioref, too_many_links);
}

bool FileBackup::handle_not_enough_space(const std::string & path)
{
	return client_main->handle_not_enough_space(path, logid);
}

std::string FileBackup::convertToOSPathFromFileClient(std::string path)
{
	if(os_file_sep()!="/")
	{
		for(size_t i=0;i<path.size();++i)
			if(path[i]=='/')
				path[i]=os_file_sep()[0];
	}
	return path;
}

std::string FileBackup::systemErrorInfo()
{
	std::string errmsg;
	int64 rc = os_last_error(errmsg);
	return trim(errmsg)+" (errorcode="+convert(rc)+")";
}

bool FileBackup::link_file(const std::string &fn, const std::string &short_fn, const std::string &curr_path,
	const std::string &os_path, const std::string& sha2, _i64 filesize, bool add_sql, FileMetadata& metadata)
{
	std::string os_curr_path=convertToOSPathFromFileClient(os_path+"/"+short_fn);
	std::string os_curr_hash_path=convertToOSPathFromFileClient(os_path+"/"+escape_metadata_fn(short_fn));
	std::string dstpath=backuppath+os_curr_path;
	std::string hashpath=backuppath_hashes+os_curr_hash_path;
	std::string filepath_old;

	bool tries_once;
	std::string ff_last;
	bool hardlink_limit;
	bool copied_file;
	int64 entryid=0;
	int entryclientid = 0;
	int64 rsize = 0;
	int64 next_entryid = 0;
	bool ok=local_hash->findFileAndLink(dstpath, NULL, hashpath, sha2, filesize, std::string(), true,
		tries_once, ff_last, hardlink_limit, copied_file, entryid, entryclientid, rsize, next_entryid,
		metadata, true, NULL);

	if(ok && add_sql)
	{
		local_hash->addFileSQL(backupid, clientid, 0, dstpath, hashpath, sha2, filesize,
			(rsize>0 && rsize!=filesize)?rsize:(copied_file?filesize:0), entryid, entryclientid, next_entryid,
			copied_file);
	}

	if(ok)
	{
		ServerLogger::Log(logid, "GT: Linked file \""+fn+"\"", LL_DEBUG);
	}
	else
	{
		if(filesize!=0)
		{
			ServerLogger::Log(logid, "GT: File \""+fn+"\" not found via hash. Loading file...", LL_DEBUG);
		}
	}

	return ok;
}

void FileBackup::sendBackupOkay(bool b_okay)
{
	if(b_okay)
	{
		notifyClientBackupSuccessful();
	}
	else
	{
		notifyClientBackupFailed();

		if(pingthread!=NULL)
		{
			pingthread->setStop(true);
			Server->getThreadPool()->waitFor(pingthread_ticket);
		}
		pingthread=NULL;
	}
}

void FileBackup::notifyClientBackupSuccessful(void)
{
	if (client_main->getProtocolVersions().cmd_version == 0)
	{
		client_main->sendClientMessageRetry("DID BACKUP", "OK", "Sending status (DID BACKUP) to client failed", 10000, 5);
	}
	else
	{
		std::string params = "status_id="+convert(status_id)+"&server_token="+EscapeParamString(server_token)
			+"&group="+convert(group);

		if (!clientsubname.empty())
		{
			params += "&clientsubname=" + EscapeParamString(clientsubname);
		}

		client_main->sendClientMessageRetry("2DID BACKUP "+ params, "OK", "Sending status (2DID BACKUP) to client failed", 10000, 5);
	}
}

void FileBackup::notifyClientBackupFailed()
{
	if (client_main->getProtocolVersions().cmd_version < 2)
		return;

	std::string params = "status_id=" + convert(status_id) + "&server_token=" + EscapeParamString(server_token)
		+ "&group=" + convert(group);

	if (!clientsubname.empty())
	{
		params += "&clientsubname=" + EscapeParamString(clientsubname);
	}

	client_main->sendClientMessageRetry("BACKUP FAILED " + params, "OK", "Sending status (BACKUP FAILED) to client failed", 10000, 2);
}

void FileBackup::waitForFileThreads(void)
{
	SStatus status=ServerStatus::getStatus(clientname);
	hashpipe->Write("flush");
	hashpipe_prepare->Write("flush");
	
	size_t hashqueuesize=std::string::npos;
	size_t prepare_hashqueuesize=0;
	while(hashqueuesize==std::string::npos || hashqueuesize>0 || prepare_hashqueuesize>0)
	{
		if (hashqueuesize != std::string::npos)
		{
			ServerStatus::setProcessQueuesize(clientname, status_id, prepare_hashqueuesize, hashqueuesize);
			Server->wait(1000);
		}

		size_t bsh_working = bsh.size() - hashpipe->getNumWaiters();
		size_t bsh_prepare_working = bsh_prepare.size() - hashpipe_prepare->getNumWaiters();
		
		hashqueuesize = hashpipe->getNumElements() + bsh_working;
		prepare_hashqueuesize = hashpipe_prepare->getNumElements() + bsh_prepare_working;
	}
	{
		Server->wait(10);

		while (hashpipe->getNumWaiters() < bsh.size())
		{
			Server->wait(1000);
		}
	}	

	ServerStatus::setProcessQueuesize(clientname, status_id, 0, 0);
}

bool FileBackup::verify_file_backup(IFile *fileentries)
{
	ServerLogger::Log(logid, "Backup verification is enabled. Verifying file backup...", LL_INFO);

	bool verify_ok=true;

	std::ostringstream log;

	log << "Verification of file backup with id " << backupid << ". Path=" << (backuppath) << " Tree-hashing=" << convert(BackupServer::useTreeHashing()) << std::endl;

	unsigned int read;
	char buffer[4096];
	std::string curr_path=backuppath;
	std::string remote_path;
	size_t verified_files=0;
	SFile cf;
	fileentries->Seek(0);
	FileListParser list_parser;
	std::stack<std::set<std::string> > folder_files;
	folder_files.push(std::set<std::string>());

	bool has_read_error = false;
	while( (read=fileentries->Read(buffer, 4096, &has_read_error))>0 )
	{
		if (has_read_error)
		{
			ServerLogger::Log(logid, "Error reading from file " + fileentries->getFilename() + ". " + os_last_error_str(), LL_ERROR);
			return false;
		}
		for(size_t i=0;i<read;++i)
		{
			std::map<std::string, std::string> extras;
			bool b=list_parser.nextEntry(buffer[i], cf, &extras);
			if(b)
			{
				std::string cfn;

				if(!cf.isdir || cf.name!="..")
				{
					cfn = fixFilenameForOS(cf.name, folder_files.top(), curr_path, false, logid, filepath_corrections);
				}

				if( !cf.isdir )
				{
					if (remote_path == "urbackup_backup_scripts"
						|| remote_path == "windows_components_config" 
						|| next(remote_path, 0, "urbackup_backup_scripts/") )
					{
						continue;
					}

					std::string sha256hex=(extras["sha256_verify"]);

					bool is_symlink = extras.find("sym_target")!=extras.end();
					bool is_special = extras.find("special") != extras.end();

					if(sha256hex.empty() && SHA_DEF_DIGEST_SIZE == 64)
					{
						//compatibility
						sha256hex = (extras["sha256"]);

						if(!sha256hex.empty())
						{
							for(size_t j=0;j<sha256hex.size();j+=2)
							{
								if(j+1<sha256hex.size())
								{
									std::swap(sha256hex[j], sha256hex[j+1]);
								}
							}
						}
					}

					if(sha256hex.empty())
					{
						std::string shabase64 = extras[sha_def_identifier];

						if (shabase64.empty())
						{
							shabase64 = extras["thash"];
						}

						if(shabase64.empty())
						{
							if (!is_special)
							{
								std::string msg = "No hash for file \"" + (curr_path + os_file_sep() + cf.name) + "\" found. Verification failed.";
								verify_ok = false;
								ServerLogger::Log(logid, msg, LL_ERROR);
								log << msg << std::endl;
							}
						}
						else
						{
							std::string local_sha = getSHADef(curr_path+os_file_sep()+cfn);

							if( !(local_sha.empty() && is_symlink) && local_sha!=base64_decode_dash(shabase64))
							{
								std::string msg="Hashes for \""+(curr_path+os_file_sep()+cf.name)+"\" differ (client side hash). Verification failed.";
								verify_ok=false;
								ServerLogger::Log(logid, msg, LL_ERROR);
								log << msg << std::endl;
								save_debug_data(remote_path+"/"+cf.name,
									base64_encode_dash(getSHADef(curr_path+os_file_sep()+cfn)),
									shabase64);
							}

							++verified_files;
						}
					}
					else
					{
						std::string local_sha = getSHA256(curr_path+os_file_sep()+cfn);

						if( !(local_sha.empty() && is_symlink) && local_sha!=sha256hex )
						{
							std::string msg="Hashes for \""+(curr_path+os_file_sep()+cf.name)+"\" differ. Verification failed.";
							verify_ok=false;
							ServerLogger::Log(logid, msg, LL_ERROR);
							log << msg << std::endl;
						}

						++verified_files;
					}
				}
				else
				{
					if(cf.name=="..")
					{
						curr_path=ExtractFilePath(curr_path, os_file_sep());
						remote_path=ExtractFilePath(remote_path, "/");
						
						folder_files.pop();
					}
					else
					{
						curr_path+=os_file_sep()+cfn;

						if(!remote_path.empty())
							remote_path+="/";

						remote_path+=cfn;

						folder_files.push(std::set<std::string>());
					}
				}
			}
		}
	}

	if(!verify_ok)
	{
		client_main->sendMailToAdmins("File backup verification failed", log.str());
	}
	else
	{
		ServerLogger::Log(logid, "Verified "+convert(verified_files)+" files", LL_DEBUG);
	}

	return verify_ok;
}

std::string FileBackup::getSHA256(const std::string& fn)
{
	sha256_ctx ctx;
	sha256_init(&ctx);

	IFile * f=Server->openFile(os_file_prefix(fn), MODE_READ);

	if(f==NULL)
	{
		return std::string();
	}

	char buffer[32768];
	unsigned int r;
	while( (r=f->Read(buffer, 32768))>0)
	{
		sha256_update(&ctx, reinterpret_cast<const unsigned char*>(buffer), r);
	}

	Server->destroy(f);

	unsigned char dig[32];
	sha256_final(&ctx, dig);

	return bytesToHex(dig, 32);
}

std::string FileBackup::getSHA512(const std::string& fn)
{
	sha512_ctx ctx;
	sha512_init(&ctx);

	IFile * f=Server->openFile(os_file_prefix(fn), MODE_READ);

	if(f==NULL)
	{
		return std::string();
	}

	char buffer[32768];
	unsigned int r;
	while( (r=f->Read(buffer, 32768))>0)
	{
		sha512_update(&ctx, reinterpret_cast<const unsigned char*>(buffer), r);
	}

	Server->destroy(f);

	std::string dig;
	dig.resize(64);
	sha512_final(&ctx, reinterpret_cast<unsigned char*>(&dig[0]));

	return dig;
}

std::string FileBackup::getSHADef(const std::string& fn)
{
	std::auto_ptr<IFsFile> f(Server->openFile(os_file_prefix(fn), MODE_READ));

	if (f.get() == NULL)
	{
		return std::string();
	}

	FsExtentIterator extent_iterator(f.get(), 512*1024);	

	if (BackupServer::useTreeHashing())
	{
		TreeHash treehash(NULL);
		if (!BackupServerPrepareHash::hash_sha(f.get(), &extent_iterator, true, treehash))
		{
			return std::string();
		}
		return treehash.finalize();
	}
	else
	{
		HashSha512 shahash;
		if (!BackupServerPrepareHash::hash_sha(f.get(), &extent_iterator, true, shahash))
		{
			return std::string();
		}
		return shahash.finalize();
	}	
}

bool FileBackup::hasDiskError()
{
	return disk_error;
}

bool FileBackup::constructBackupPath(bool on_snapshot, bool create_fs, std::string& errmsg)
{
	if(!createDirectoryForClient())
	{
		return false;
	}

	time_t tt=time(NULL);
#ifdef _WIN32
	tm lt;
	tm *t=&lt;
	localtime_s(t, &tt);
#else
	tm *t=localtime(&tt);
#endif
	char buffer[500];
	strftime(buffer, 500, "%y%m%d-%H%M", t);
	backuppath_single=(std::string)buffer;
	std::string backupfolder=server_settings->getSettings()->backupfolder;
	backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath_single;
	backuppath_hashes=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath_single+os_file_sep()+".hashes";

	dir_pool_path = backupfolder + os_file_sep() + clientname + os_file_sep() + ".directory_pool";

	if(on_snapshot)
	{
		if(create_fs)
		{
			bool zfs_file = BackupServer::getSnapshotMethod(false) == BackupServer::ESnapshotMethod_ZfsFile;
			if (zfs_file)
			{
				if (os_get_file_type(backuppath) != 0)
				{
					ServerLogger::Log(logid, "File/Directory " + backuppath + " already exists.", LL_ERROR);
					allow_remove_backup_folder = false;
					return false;
				}

				std::auto_ptr<IFile> touch_f(Server->openFile(backuppath, MODE_WRITE));
				if (touch_f.get() == NULL)
				{
					ServerLogger::Log(logid, "Could not touch file " + backuppath + ". " + os_last_error_str(), LL_ERROR);
					return false;
				}
				touch_f->Sync();
			}

			if (SnapshotHelper::isSubvolume(false, clientname, backuppath_single))
			{
				if (zfs_file)
				{
					Server->deleteFile(backuppath);
				}

				ServerLogger::Log(logid, "Subvolume " + clientname + "/"+backuppath_single + " already exists.", LL_ERROR);
				allow_remove_backup_folder = false;
				return false;
			}

			bool b = SnapshotHelper::createEmptyFilesystem(false, clientname, backuppath_single, errmsg);

			if (!b)
			{
				if (zfs_file)
				{
					Server->deleteFile(backuppath);
				}

				ServerLogger::Log(logid, "Error creating empty file subvolume. "
					+ (errmsg.empty() ? "" : ("\"" + errmsg + "\"")), LL_ERROR);

				return false;
			}

			if (zfs_file)
			{
				std::string mountpoint = SnapshotHelper::getMountpoint(false, clientname, backuppath_single);
				if (mountpoint.empty())
				{
					ServerLogger::Log(logid, "Could not find mountpoint of snapshot of client " + clientname + " path " + backuppath_single, LL_ERROR);
					return false;
				}

				if (!os_link_symbolic(mountpoint, backuppath + "_new"))
				{
					ServerLogger::Log(logid, "Could create symlink to mountpoint at " + backuppath+ " to " + mountpoint + ". " + os_last_error_str(), LL_ERROR);
					return false;
				}

				if (!os_rename_file(backuppath + "_new", backuppath))
				{
					ServerLogger::Log(logid, "Could rename symlink at " + backuppath + "_new to " + backuppath + ". " + os_last_error_str(), LL_ERROR);
					return false;
				}
			}

			b = os_create_dir(os_file_prefix(backuppath_hashes))
				|| (os_directory_exists(os_file_prefix(backuppath_hashes))
					&& getFiles(os_file_prefix(backuppath_hashes)).empty());

			if (!b)
			{
				ServerLogger::Log(logid, "Error creating hash folder. " + os_last_error_str(), LL_ERROR);
				return false;
			}

			return true;
		}
		else
		{
			return true;
		}
	}
	else
	{
		return os_create_dir(os_file_prefix(backuppath)) && os_create_dir(os_file_prefix(backuppath_hashes));	
	}
}

bool FileBackup::constructBackupPathCdp()
{
	time_t tt=time(NULL);
#ifdef _WIN32
	tm lt;
	tm *t=&lt;
	localtime_s(t, &tt);
#else
	tm *t=localtime(&tt);
#endif
	char buffer[500];
	strftime(buffer, 500, "%y%m%d-%H%M", t);
	backuppath_single="continuous_"+std::string(buffer);
	std::string backupfolder=server_settings->getSettings()->backupfolder;
	backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath_single;
	backuppath_hashes=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath_single+os_file_sep()+".hashes";

	if( os_directory_exists(os_file_prefix(backuppath)) && os_directory_exists(os_file_prefix(backuppath_hashes)))
	{
		return true;
	}

	return os_create_dir(os_file_prefix(backuppath)) && os_create_dir(os_file_prefix(backuppath_hashes));	
}

void FileBackup::createUserViews(IFile* file_list_f)
{
	std::auto_ptr<ISettingsReader> urbackup_tokens(
		Server->createFileSettingsReader(os_file_prefix(backuppath_hashes+os_file_sep()+".urbackup_tokens.properties")));

	if(urbackup_tokens.get()==NULL)
	{
		ServerLogger::Log(logid, "Cannot create user view. Token file not present.", LL_WARNING);
		return;
	}

	std::string s_real_uids = urbackup_tokens->getValue("real_uids", "");
	std::vector<std::string> uids;
	Tokenize(s_real_uids, uids, ",");

	for(size_t i=0;i<uids.size();++i)
	{
		int64 uid = os_atoi64(uids[i]);
		std::string s_gids = urbackup_tokens->getValue(uids[i]+".gids", "");
		std::vector<std::string> gids;
		Tokenize(s_gids, gids, ",");
		std::vector<int64> ids;
		ids.push_back(uid);
		for(size_t j=0;j<gids.size();++j)
		{
			ids.push_back(os_atoi64(gids[j]));
		}

		std::string accountname = base64_decode_dash(urbackup_tokens->getValue(uids[i]+".accountname", std::string()));
		accountname = greplace("/", "_", accountname);
		accountname = greplace("\\", "_", accountname);
		std::vector<size_t> identical_permission_roots = findIdenticalPermissionRoots(file_list_f, ids);
		if(!createUserView(file_list_f, ids, accountname, identical_permission_roots))
		{
			ServerLogger::Log(logid, "Error creating user view for user with id "+convert(uid), LL_WARNING);
		}
	}
}

namespace
{
	struct SDirStatItem
	{
		size_t has_perm;
		size_t id;
		size_t nodecount;
		size_t identicalcount;
	};
}

std::vector<size_t> FileBackup::findIdenticalPermissionRoots(IFile* file_list_f, const std::vector<int64>& ids)
{
	file_list_f->Seek(0);

	char buffer[4096];
	_u32 bread;
	FileListParser file_list_parser;
	std::stack<SDirStatItem> dir_permissions;
	size_t curr_id = 0;
	std::vector<size_t> identical_permission_roots;
	SFile data;
	std::stack<std::set<std::string> > folder_files;
	folder_files.push(std::set<std::string>());

	std::string curr_path;
	std::string metadata_home_path = backuppath + os_file_sep() + ".hashes";

	while((bread=file_list_f->Read(buffer, 4096))>0)
	{
		for(_u32 i=0;i<bread;++i)
		{
			std::map<std::string, std::string> extra;
			if(file_list_parser.nextEntry(buffer[i], data, &extra))
			{

				std::string osspecific_name;
				std::string permissions;

				if(!data.isdir || data.name!="..")
				{
					osspecific_name = fixFilenameForOS(data.name, folder_files.top(), curr_path, false, logid, filepath_corrections);
				}

				if ( curr_path == os_file_sep() + "urbackup_backup_scripts"
					&& !data.isdir
					&& os_directory_exists(os_file_prefix(metadata_home_path + curr_path + os_file_sep() + osspecific_name)) )
				{
					data.isdir = true;
				}

				if(data.isdir)
				{
					if(data.name=="..")
					{
						folder_files.pop();
						curr_path = ExtractFilePath(curr_path, os_file_sep());
					}
					else
					{
						folder_files.push(std::set<std::string>());

						std::string metadata_fn=metadata_home_path + curr_path + os_file_sep() + osspecific_name + os_file_sep() + metadata_dir_fn;

						str_map::iterator sym_target = extra.find("sym_target");
						if(sym_target!=extra.end())
						{
							metadata_fn=metadata_home_path + curr_path + os_file_sep() + escape_metadata_fn(osspecific_name);
						}

						FileMetadata metadata;
						if(!read_metadata(metadata_fn, metadata))
						{
							ServerLogger::Log(logid, "Error reading metadata of "+curr_path, LL_WARNING);
						}
						else
						{
							permissions = metadata.file_permissions;
						}

						curr_path += os_file_sep() + osspecific_name;
					}
				}
				else
				{
					std::string metadata_fn=metadata_home_path + curr_path + os_file_sep() + escape_metadata_fn(osspecific_name);
					std::string filename = curr_path + os_file_sep() + osspecific_name;

					FileMetadata metadata;
					if(!read_metadata(metadata_fn, metadata))
					{
						ServerLogger::Log(logid, "Error reading metadata of "+filename, LL_WARNING);
					}
					else
					{
						permissions = metadata.file_permissions;
					}
				}

				size_t has_perm=0;
				for(size_t j=0;j<ids.size();++j)
				{
					bool denied=false;
					if(FileMetadata::hasPermission(permissions, ids[j], denied))
					{
						++has_perm;
					}
				}

				if(data.isdir)
				{
					if(data.name=="..")
					{
						SDirStatItem last_dir = {};

						if(!dir_permissions.empty())
						{
							if(dir_permissions.top().nodecount==
								dir_permissions.top().identicalcount)
							{
								identical_permission_roots.push_back(dir_permissions.top().id);
							}

							last_dir = dir_permissions.top();
						}

						dir_permissions.pop();

						if(!dir_permissions.empty())
						{
							dir_permissions.top().nodecount+=last_dir.nodecount+1;
							dir_permissions.top().identicalcount+=last_dir.identicalcount;

							if(last_dir.has_perm==dir_permissions.top().has_perm)
							{
								++dir_permissions.top().identicalcount;
							}
						}
					}
					else
					{
						SDirStatItem nsi = {
							has_perm,
							curr_id,
							0,
							0
						};

						dir_permissions.push(nsi);
					}
				}
				else
				{
					if(!dir_permissions.empty())
					{
						++dir_permissions.top().nodecount;
						if(has_perm==dir_permissions.top().has_perm)
						{
							++dir_permissions.top().identicalcount;
						}
					}
				}

				++curr_id;
			}
		}
	}

	std::sort(identical_permission_roots.begin(), identical_permission_roots.end());
	return identical_permission_roots;
}

bool FileBackup::createUserView(IFile* file_list_f, const std::vector<int64>& ids, std::string accoutname, const std::vector<size_t>& identical_permission_roots)
{
	std::string user_view_home_path = backuppath + os_file_sep() + "user_views" + os_file_sep() + accoutname;

	if(os_directory_exists(os_file_prefix(user_view_home_path)))
	{
		os_remove_nonempty_dir(os_file_prefix(user_view_home_path));
	}

	if(!os_create_dir_recursive(os_file_prefix(user_view_home_path)))
	{
		ServerLogger::Log(logid, "Error creating folder \""+user_view_home_path+"\" for user at user_views in backup storage of current backup", LL_WARNING);
		return false;
	}

	file_list_f->Seek(0);

	char buffer[4096];
	_u32 bread;
	FileListParser file_list_parser;
	std::string curr_path;
	std::string metadata_home_path = backuppath + os_file_sep() + ".hashes";
	size_t skip = 0;
	size_t id = 0;
	SFile data;
	std::stack<std::set<std::string> > folder_files;
	folder_files.push(std::set<std::string>());
	
	bool has_read_error = false;
	while((bread=file_list_f->Read(buffer, 4096, &has_read_error))>0)
	{
		if (has_read_error)
		{
			ServerLogger::Log(logid, "Error reading from file " + file_list_f->getFilename() + ". " + os_last_error_str(), LL_ERROR);
			return false;
		}
		for(_u32 i=0;i<bread;++i)
		{
			std::map<std::string, std::string> extra;
			if(file_list_parser.nextEntry(buffer[i], data, &extra))
			{
				if(skip>0)
				{
					if(data.isdir)
					{
						if(data.name=="..")
						{
							--skip;

							if(skip==0)
							{
								curr_path = ExtractFilePath(curr_path, os_file_sep());
								folder_files.pop();
							}
						}
						else
						{
							++skip;
						}
					}
					++id;
					continue;
				}

				std::string osspecific_name;

				if(!data.isdir || data.name!="..")
				{
					osspecific_name = fixFilenameForOS(data.name, folder_files.top(), curr_path, false, logid, filepath_corrections);
				}

				if (curr_path == os_file_sep() + "urbackup_backup_scripts"
					&& !data.isdir
					&& os_directory_exists(os_file_prefix(metadata_home_path + curr_path + os_file_sep() + osspecific_name)))
				{
					data.isdir = true;
				}

				if(data.isdir)
				{
					if(data.name=="..")
					{
						folder_files.pop();
						curr_path = ExtractFilePath(curr_path, os_file_sep());
					}
					else
					{
						folder_files.push(std::set<std::string>());

						std::string metadata_fn=metadata_home_path + curr_path + os_file_sep() + osspecific_name + os_file_sep() + metadata_dir_fn;

						str_map::iterator sym_target = extra.find("sym_target");
						if(sym_target!=extra.end())
						{
							metadata_fn=metadata_home_path + curr_path + os_file_sep() + escape_metadata_fn(osspecific_name);
						}

						FileMetadata metadata;
						if(!read_metadata(metadata_fn, metadata))
						{
							ServerLogger::Log(logid, "Error reading metadata of "+curr_path, LL_WARNING);
						}

						curr_path += os_file_sep() + osspecific_name;

						bool has_perm = false;
						for(size_t j=0;j<ids.size();++j)
						{
							bool denied=false;
							if(FileMetadata::hasPermission(metadata.file_permissions, ids[j], denied))
							{
								if(std::binary_search(identical_permission_roots.begin(),
									identical_permission_roots.end(), id))
								{
									if(!os_link_symbolic(backuppath + curr_path,
										os_file_prefix(user_view_home_path + curr_path)))
									{
										ServerLogger::Log(logid, "Error creating symbolic link at \""+user_view_home_path + curr_path+"\" for user view (directory). "+os_last_error_str(), LL_WARNING);
										return false;
									}
									skip=1;
								}
								else
								{
									if(!os_create_dir(os_file_prefix(user_view_home_path + curr_path)))
									{
										ServerLogger::Log(logid, "Error creating directory \""+user_view_home_path+curr_path+"\" for user view. "+os_last_error_str(), LL_WARNING);
										return false;
									}
								}
								has_perm=true;
								break;
							}
						}
						
						if(!has_perm)
						{
							skip=1;
						}
					}
				}
				else
				{
					std::string metadata_fn=metadata_home_path + curr_path + os_file_sep() + escape_metadata_fn(osspecific_name);
					std::string filename = curr_path + os_file_sep() + osspecific_name;

					FileMetadata metadata;
					if(!read_metadata(metadata_fn, metadata))
					{
						ServerLogger::Log(logid, "Error reading metadata of "+filename, LL_WARNING);
					}

					for(size_t j=0;j<ids.size();++j)
					{
						bool denied=false;
						if(FileMetadata::hasPermission(metadata.file_permissions, ids[j], denied))
						{
							if(!os_link_symbolic(backuppath + filename,
								os_file_prefix(user_view_home_path + filename)))
							{
								ServerLogger::Log(logid, "Error creating symbolic link at \""+user_view_home_path + filename+"\" for user view (file)", LL_WARNING);
								return false;
							}
							break;
						}
					}
				}

				++id;
			}
		}
	}

	std::string backupfolder = server_settings->getSettings()->backupfolder;
	std::string o_user_view_folder = backupfolder+os_file_sep()+"user_views" + os_file_sep()+clientname+ os_file_sep()+(accoutname);

	if(!os_directory_exists(os_file_prefix(o_user_view_folder)) &&
		!os_create_dir_recursive(o_user_view_folder))
	{
		ServerLogger::Log(logid, "Error creating folder for user at user_views in backup storage", LL_WARNING);
		return false;
	}

	if(!os_link_symbolic(os_file_prefix(user_view_home_path),
		os_file_prefix(o_user_view_folder + os_file_sep() + backuppath_single)))
	{
		ServerLogger::Log(logid, "Error creating user view link at user_views in backup storage", LL_WARNING);
		return false;
	}

	os_remove_symlink_dir(os_file_prefix(o_user_view_folder + os_file_sep() + "current"));
	if(!os_link_symbolic(os_file_prefix(user_view_home_path),
		os_file_prefix(o_user_view_folder + os_file_sep() + "current")))
	{
		ServerLogger::Log(logid, "Error creating current user view link at user_views in backup storage", LL_WARNING);
		return false;
	}

	return true;
}

void FileBackup::saveUsersOnClient()
{
	std::auto_ptr<ISettingsReader> urbackup_tokens(
		Server->createFileSettingsReader(os_file_prefix(backuppath_hashes+os_file_sep()+".urbackup_tokens.properties")));

	if(urbackup_tokens.get()==NULL)
	{
		ServerLogger::Log(logid, "Cannot determine users on client. Token file not present.", LL_WARNING);
		return;
	}

	std::string s_uids = urbackup_tokens->getValue("real_uids", "");
	std::vector<std::string> uids;
	Tokenize(s_uids, uids, ",");

	backup_dao->deleteAllUsersOnClient(clientid);

	for(size_t i=0;i<uids.size();++i)
	{
		std::string accountname = (base64_decode_dash(urbackup_tokens->getValue(uids[i]+".accountname", std::string())));
		backup_dao->addUserOnClient(clientid, accountname);

		backup_dao->addUserToken(accountname, clientid, urbackup_tokens->getValue(uids[i]+".token", std::string()));

		std::string s_gids = urbackup_tokens->getValue(uids[i]+".gids", "");
		std::vector<std::string> gids;
		Tokenize(s_gids, gids, ",");

		for(size_t j=0;j<gids.size();++j)
		{
			std::string groupname = (base64_decode_dash(urbackup_tokens->getValue(gids[j] + ".accountname", std::string())));

			backup_dao->addUserTokenWithGroup(accountname, clientid, urbackup_tokens->getValue(gids[j]+".token", std::string()), groupname);
		}
	}

	s_uids = urbackup_tokens->getValue("ids", "");
	uids.clear();
	Tokenize(s_uids, uids, ",");

	for (size_t i = 0; i < uids.size(); ++i)
	{
		std::string accountname = (base64_decode_dash(urbackup_tokens->getValue(uids[i] + ".accountname", std::string())));
		if (accountname == "root")
		{
			backup_dao->addUserToken(accountname, clientid, urbackup_tokens->getValue(uids[i] + ".token", std::string()));
			break;
		}
	}

	std::vector<std::string> keys = urbackup_tokens->getKeys();
	for(size_t i=0;i<keys.size();++i)
	{
		if(keys[i].find(".token")==keys[i].size()-6)
		{
			backup_dao->addClientToken(clientid, urbackup_tokens->getValue(keys[i], std::string()));
		}
	}
}

void FileBackup::deleteBackup()
{
	if(backupid==-1)
	{
		if (!allow_remove_backup_folder)
		{
			return;
		}

		if(use_snapshots)
		{
			if(!SnapshotHelper::removeFilesystem(false, clientname, backuppath_single) )
			{
				ServerLinkDao link_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS));
				remove_directory_link_dir(backuppath, link_dao, clientid);
			}
			else if (BackupServer::getSnapshotMethod(false) == BackupServer::ESnapshotMethod_ZfsFile)
			{
				Server->deleteFile(backuppath);
			}
		}
		else
		{
			ServerLinkDao link_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS));
			remove_directory_link_dir(backuppath, link_dao, clientid);
		}	
	}
	else
	{				
		if (!allow_remove_backup_folder)
		{
			ServerCleanupDao cleanupdao(db);
			cleanupdao.removeFileBackup(backupid);
			return;
		}

		Server->getThreadPool()->executeWait(
			new ServerCleanupThread(CleanupAction(server_settings->getSettings()->backupfolder, clientid, backupid, true, NULL) ),
			"delete fbackup");
	}
}

bool FileBackup::createSymlink(const std::string& name, size_t depth, const std::string& symlink_target, const std::string& dir_sep, bool isdir )
{
	std::vector<std::string> toks;
	Tokenize(symlink_target, toks, dir_sep);

	std::string target;

	for(size_t i=0;i<depth;++i)
	{
		target+=".."+os_file_sep();
	}

	for(size_t i=0;i<toks.size();++i)
	{
		std::set<std::string> emptyset;
		std::string emptypath;
		std::string component = fixFilenameForOS(toks[i], emptyset, emptypath, true, logid, filepath_corrections);

		if(component==".." || component==".")
			continue;

		target+=component;

		if(i+1<toks.size())
		{
			target+=os_file_sep();
		}
	}

	if (toks.empty()
		&& isdir)
	{
		target += ".symlink_void_dir";
	}

	if (toks.empty()
		&& !isdir)
	{
		target += ".symlink_void_file";
	}

	return os_link_symbolic(target, os_file_prefix(name), NULL, &isdir);
}

bool FileBackup::startFileMetadataDownloadThread()
{

	if(client_main->getProtocolVersions().file_meta>0)
	{
		std::string identity = client_main->getIdentity();
		std::auto_ptr<FileClient> fc_metadata_stream(new FileClient(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
			client_main->isOnInternetConnection(), client_main, use_tmpfiles?NULL:this));

		_u32 rc=client_main->getClientFilesrvConnection(fc_metadata_stream.get(), server_settings.get(), 60000);
		if(rc!=ERR_CONNECTED)
		{
			ServerLogger::Log(logid, "Backup of "+clientname+" failed - CONNECT error (for metadata stream)", LL_ERROR);
			has_early_error=true;
			log_backup=false;
			return false;
		}

		fc_metadata_stream->setProgressLogCallback(this);

        metadata_download_thread.reset(new server::FileMetadataDownloadThread(fc_metadata_stream.release(), server_token,
			logid, backupid, clientid, use_tmpfiles, tmpfile_path));

		metadata_download_thread_ticket = Server->getThreadPool()->execute(metadata_download_thread.get(), "fbackup meta");

		int64 starttime=Server->getTimeMS();

		do
		{
			if(metadata_download_thread->isDownloading())
			{
				break;
			}
			Server->wait(100);
		}
		while(Server->getTimeMS()-starttime<10000);

		if(!metadata_download_thread->isDownloading())
		{
			stopFileMetadataDownloadThread(true, 0);
			return false;
		}

		local_hash2.reset(new BackupServerHash(NULL, clientid, use_snapshots, use_reflink, use_tmpfiles, logid, use_snapshots, max_file_id));

		metadata_apply_thread.reset(new server::FileMetadataDownloadThread::FileMetadataApplyThread(metadata_download_thread.get(),
			backuppath_hashes, backuppath, client_main, local_hash2.get(), filepath_corrections, max_file_id));

		metadata_apply_thread_ticket = Server->getThreadPool()->execute(metadata_apply_thread.get(), "fb meta apply");
	}	

	return true;
}

bool FileBackup::stopFileMetadataDownloadThread(bool stopped, size_t expected_embedded_metadata_files)
{
	const int64 metadata_waittime = 140000;

	if(metadata_download_thread.get()!=NULL)
	{
		metadata_download_thread->forceStart();

		if (!Server->getThreadPool()->waitFor(metadata_download_thread_ticket, 1000))
		{
			ServerLogger::Log(logid, "Waiting for metadata download stream to finish", LL_INFO);

			int64 transferred_bytes = metadata_download_thread->getTransferredBytes();
			int64 last_transfer_time = Server->getTimeMS();
			int attempt = 0;

			do
			{
				std::string identity = client_main->getIdentity();
				std::auto_ptr<FileClient> fc_metadata_stream_end(new FileClient(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
					client_main->isOnInternetConnection(), client_main, use_tmpfiles ? NULL : this));

				_u32 rc = client_main->getClientFilesrvConnection(fc_metadata_stream_end.get(), server_settings.get(), 10000);
				if (rc == ERR_CONNECTED)
				{
					fc_metadata_stream_end->InformMetadataStreamEnd(server_token, 0);
				}
				else
				{
					ServerLogger::Log(logid, "Could not get filesrv connection when trying to stop meta-data tranfer (" + clientname + ").", LL_DEBUG);
				}

				if (Server->getThreadPool()->waitFor(metadata_download_thread_ticket, 10000))
				{
					break;
				}

				int64 new_transferred_bytes = metadata_download_thread->getTransferredBytes();

				if (new_transferred_bytes > transferred_bytes)
				{
					last_transfer_time = Server->getTimeMS();
				}
				else
				{
					ServerLogger::Log(logid, "Waiting for metadata download stream to finish (attempt " + convert(attempt) + ", " + clientname + "). "
						+ PrettyPrintTime(Server->getTimeMS() - last_transfer_time) + " since last meta-data transfer. Forcefully shutting down after "
						+ PrettyPrintTime(metadata_waittime) + " without transfer.", LL_DEBUG);
				}

				if (Server->getTimeMS() - last_transfer_time > metadata_waittime)
				{
					ServerLogger::Log(logid, "No meta-data transfer in the last " + PrettyPrintTime(Server->getTimeMS() - last_transfer_time) + ". Shutting down meta-data tranfer.", LL_DEBUG);
					metadata_download_thread->shutdown();
				}

				if (attempt > 0)
				{
					metadata_download_thread->setProgressLogEnabled(true);
				}

				transferred_bytes = new_transferred_bytes;
				++attempt;
			} while (true);
		}

		if (metadata_download_thread->getHasError() || metadata_download_thread->getHasTimeoutError())
		{
			++num_issues;
		}

		Server->getThreadPool()->waitFor(metadata_apply_thread_ticket);

		if(!stopped && !disk_error && !has_early_error && ( !metadata_download_thread->getHasError() || metadata_download_thread->getHasTimeoutError() ) )
		{	
			if (!metadata_apply_thread->hasSuccess())
			{
				++num_issues;
			}

			if (!metadata_apply_thread->hasSuccess()
				&& metadata_download_thread->getHasFatalError())
			{
				disk_error = true;
			}
			else if (metadata_apply_thread->getNumEmbeddedFiles() != expected_embedded_metadata_files)
			{
				ServerLogger::Log(logid, "Wrong number of embedded meta-data files. Expected "+convert(expected_embedded_metadata_files)+" but got "+convert(metadata_apply_thread->getNumEmbeddedFiles()), LL_ERROR);
				disk_error = true;
			}

			return metadata_apply_thread->hasSuccess();
		}
	}

	return true;
}

void FileBackup::parseSnapshotFailed(const std::string & logline)
{
	std::string share = getbetween("Creating snapshot of \"", "\" failed", logline);
	if (!share.empty())
	{
		shares_without_snapshot.push_back(share);
	}

	share = getbetween("Creating shadowcopy of \"", "\" failed in indexDirs().", logline);
	if (!share.empty())
	{
		shares_without_snapshot.push_back(share);
	}

	share = getbetween("Backing up \"", "\" without snapshot.", logline);
	if (!share.empty())
	{
		shares_without_snapshot.push_back(share);
	}
}

std::string FileBackup::permissionsAllowAll()
{
	CWData token_info;
	//allow to all
	token_info.addChar(0);
	token_info.addVarInt(0);

	return std::string(token_info.getDataPtr(), token_info.getDataSize());
}

bool FileBackup::loadWindowsBackupComponentConfigXml(FileClient &fc)
{
	std::string component_config_dir = backuppath + os_file_sep() + "windows_components_config";
	if (os_directory_exists(os_file_prefix(component_config_dir)))
	{
		ServerLogger::Log(logid, "Loading Windows backup component config XML...", LL_DEBUG);
		std::auto_ptr<IFsFile> component_config_xml(Server->openFile(os_file_prefix(component_config_dir+os_file_sep()+"backupcom.xml"), MODE_WRITE));
		_u32 rc = fc.GetFile(server_token + "|windows_components_config/backupcom.xml", component_config_xml.get(), true, false, 0, false, 0);
		if (rc != ERR_SUCCESS)
		{
			ServerLogger::Log(logid, "Error getting Windows backup component config XML: " + fc.getErrorString(rc), LL_ERROR);
			return false;
		}
		if (component_config_xml->Size() == 0)
		{
			ServerLogger::Log(logid, "Windows backup component config XML is empty", LL_ERROR);
			return false;
		}
	}

	return true;
}

bool FileBackup::startPhashDownloadThread(const std::string& async_id)
{
	std::string identity = client_main->getIdentity();
	std::auto_ptr<FileClient> fc_phash_stream(new FileClient(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
		client_main->isOnInternetConnection(), client_main, use_tmpfiles ? NULL : this));

	_u32 rc = client_main->getClientFilesrvConnection(fc_phash_stream.get(), server_settings.get(), 60000);
	if (rc != ERR_CONNECTED)
	{
		ServerLogger::Log(logid, "Full Backup of " + clientname + " failed - CONNECT error (for metadata stream)", LL_ERROR);
		has_early_error = true;
		log_backup = false;
		return false;
	}

	filelist_async_id = async_id;
	fc_phash_stream->setProgressLogCallback(this);
	phash_load.reset(new PhashLoad(fc_phash_stream.release(), logid, async_id));
	phash_load_ticket = Server->getThreadPool()->execute(phash_load.get(), "phash load");

	int64 starttime = Server->getTimeMS();

	do
	{
		if (phash_load->isDownloading())
		{
			break;
		}
		Server->wait(100);
	} while (Server->getTimeMS() - starttime<10000);

	if (!phash_load->isDownloading())
	{
		stopPhashDownloadThread(async_id);
		return false;
	}

	return true;
}

bool FileBackup::stopPhashDownloadThread(const std::string& async_id)
{
	if (phash_load.get() == NULL)
	{
		return true;
	}

	if (!Server->getThreadPool()->waitFor(phash_load_ticket, 1000))
	{
		ServerLogger::Log(logid, "Waiting for parallel hash load stream to finish", LL_INFO);

		do
		{
			std::string identity = client_main->getIdentity();
			std::auto_ptr<FileClient> fc_phash_stream_end(new FileClient(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
				client_main->isOnInternetConnection(), client_main, use_tmpfiles ? NULL : this));

			_u32 rc = client_main->getClientFilesrvConnection(fc_phash_stream_end.get(), server_settings.get(), 10000);
			if (rc == ERR_CONNECTED)
			{
				fc_phash_stream_end->StopPhashLoad(server_token, async_id, 0);
			}
			else
			{
				ServerLogger::Log(logid, "Could not get filesrv connection when trying to end parallel hash data tranfer (" + clientname + ").", LL_DEBUG);
			}

			if (Server->getThreadPool()->waitFor(phash_load_ticket, 1000))
			{
				break;
			}

			phash_load->setProgressLogEnabled(true);
			phash_load->shutdown();

		} while (true);
	}

	if (phash_load->hasError() || phash_load->hasTimeoutError())
	{
		++num_issues;
	}

	phash_load.reset();

	return true;
}

void FileBackup::save_debug_data(const std::string& rfn, const std::string& local_hash, const std::string& remote_hash)
{
	ServerLogger::Log(logid, "Local hash: "+local_hash+" remote hash: "+remote_hash, LL_INFO);
	ServerLogger::Log(logid, "Trying to download "+rfn, LL_INFO);

	std::string identity = client_main->getIdentity();
	FileClient fc(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
		client_main->isOnInternetConnection(), client_main, use_tmpfiles?NULL:this);

	_u32 rc=client_main->getClientFilesrvConnection(&fc, server_settings.get(), 10000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(logid, "Cannot connect to retrieve file that failed to verify - CONNECT error", LL_ERROR);
		return;
	}

	fc.setProgressLogCallback(this);

	std::auto_ptr<IFile> tmpfile(Server->openTemporaryFile());
	std::string tmpdirname = tmpfile->getFilename();
	tmpfile.reset();
	Server->deleteFile(tmpdirname);
	os_create_dir(tmpdirname);

	std::auto_ptr<IFsFile> output_file(Server->openFile(tmpdirname+os_file_sep()+"verify_failed.file", MODE_WRITE));
	rc = fc.GetFile((rfn), output_file.get(), true, false, 0, false, 0);

	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error downloading "+rfn+" after verification failed. Errorcode: " + FileClient::getErrorString(rc) + " (" + convert(rc) + ")", LL_ERROR);
	}
	else
	{
		output_file.reset();
		std::string sha512 = base64_encode_dash(getSHA512(tmpdirname+os_file_sep()+"verify_failed.file"));
		std::string sha256 = getSHA256(tmpdirname+os_file_sep()+"verify_failed.file");
		ServerLogger::Log(logid, "Downloaded file "+rfn+" with failed verification to "+tmpdirname+" for analysis. "
			" SHA512: "+sha512+" SHA256: "+sha256, LL_INFO);
	}
}
