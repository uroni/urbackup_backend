/*************************************************************************
*    UrBackup - Client/Server backup system
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

#include "FullFileBackup.h"
#include "database.h"
#include <vector>
#include "../Interface/Server.h"
#include "dao/ServerBackupDao.h"
#include "ClientMain.h"
#include "server_log.h"
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../urbackupcommon/filelist_utils.h"
#include "server_running.h"
#include "ServerDownloadThreadGroup.h"
#include "../urbackupcommon/file_metadata.h"
#include "FileMetadataDownloadThread.h"
#include "snapshot_helper.h"
#include <stack>
#include "PhashLoad.h"
#include "server.h"

extern std::string server_identity;


FullFileBackup::FullFileBackup( ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname,
	LogAction log_action, int group, bool use_tmpfiles, std::string tmpfile_path, bool use_reflink, bool use_snapshots,
	std::string server_token, std::string details, bool scheduled)
	: FileBackup(client_main, clientid, clientname, clientsubname, log_action, false, group,
	use_tmpfiles, tmpfile_path, use_reflink, use_snapshots, server_token, details, scheduled)
{

}


SBackup FullFileBackup::getLastFullDurations( void )
{
	std::vector<ServerBackupDao::SDuration> durations = 
		backup_dao->getLastFullDurations(clientid);

	ServerBackupDao::SDuration duration = interpolateDurations(durations);

	SBackup b;

	b.indexing_time_ms = duration.indexing_time_ms;
	b.backup_time_ms = duration.duration*1000;

	return b;
}

bool FullFileBackup::doFileBackup()
{
	ServerLogger::Log(logid, std::string("Starting ") + (scheduled ? "scheduled" : "unscheduled") + " full file backup...", LL_INFO);

	SBackup last_backup_info = getLastFullDurations();

	int64 eta_set_time = Server->getTimeMS();
	ServerStatus::setProcessEta(clientname, status_id, last_backup_info.backup_time_ms + last_backup_info.indexing_time_ms, eta_set_time);

	int64 indexing_start_time = Server->getTimeMS();

	bool no_backup_dirs=false;
	bool connect_fail=false;
	bool b=request_filelist_construct(true, false, group, true, no_backup_dirs, connect_fail, clientsubname);
	if(!b)
	{
		has_early_error=true;

		if (no_backup_dirs)
		{
			backup_dao->updateClientNumIssues(ServerBackupDao::num_issues_no_backuppaths, clientid);
		}

		if(no_backup_dirs || connect_fail)
		{
			log_backup=false;
		}
		else
		{
			log_backup=true;
		}

		return false;
	}

	bool hashed_transfer=true;
	bool save_incomplete_files=false;

	if(client_main->isOnInternetConnection())
	{
		if(server_settings->getSettings()->internet_full_file_transfer_mode=="raw")
			hashed_transfer=false;
		if(server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash")
			save_incomplete_files=true;
	}
	else
	{
		if(server_settings->getSettings()->local_full_file_transfer_mode=="raw")
			hashed_transfer=false;
		if(server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash")
			save_incomplete_files=true;
	}

	if(hashed_transfer)
	{
		ServerLogger::Log(logid, clientname+": Doing backup with hashed transfer...", LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(logid, clientname+": Doing backup without hashed transfer...", LL_DEBUG);
	}
	std::string identity = client_main->getIdentity();
	FileClient fc(false, identity, client_main->getProtocolVersions().filesrv_protocol_version,
		client_main->isOnInternetConnection(), client_main, use_tmpfiles?NULL:this);
	_u32 rc=client_main->getClientFilesrvConnection(&fc, server_settings.get(), 60000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(logid, "Full Backup of "+clientname+" failed - CONNECT error", LL_ERROR);
		has_early_error=true;
		log_backup=false;
		return false;
	}
	fc.setProgressLogCallback(this);

	IFsFile* tmp_filelist = ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
	ScopedDeleteFile tmp_filelist_delete(tmp_filelist);
	if(tmp_filelist==NULL) 
	{
		ServerLogger::Log(logid, "Error creating temporary file in ::doFullBackup", LL_ERROR);
		return false;
	}

	ServerLogger::Log(logid, clientname+": Loading file list...", LL_INFO);

	int64 full_backup_starttime=Server->getTimeMS();

	rc=fc.GetFile(group>0?("urbackup/filelist_"+convert(group)+".ub"):"urbackup/filelist.ub", tmp_filelist, hashed_transfer, false, 0, false, 0);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error getting filelist of "+clientname+". Errorcode: "+fc.getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		has_early_error=true;
		return false;
	}

	getTokenFile(fc, hashed_transfer, false);

	if (!backup_dao->newFileBackup(0, clientid, backuppath_single, 0, Server->getTimeMS() - indexing_start_time, group))
	{
		ServerLogger::Log(logid, "Error creating new backup row in database", LL_ERROR);
		has_early_error = true;
		return false;
	}

	backupid = static_cast<int>(db->getLastInsertID());

	tmp_filelist->Seek(0);

	FileListParser list_parser;

	Server->deleteFile(clientlistName(backupid));
	IFile *clientlist=Server->openFile(clientlistName(backupid), MODE_RW_CREATE);
	ScopedDeleteFile clientlist_delete(clientlist);

	if(clientlist==NULL )
	{
		ServerLogger::Log(logid, "Error creating clientlist for client "+clientname+" at "+Server->getServerWorkingDir()+ "/"+clientlistName(backupid)+". "+os_last_error_str(), LL_ERROR);
		has_early_error=true;
		return false;
	}

	if(ServerStatus::getProcess(clientname, status_id).stop)
	{
		ServerLogger::Log(logid, "Server admin stopped backup. -1", LL_ERROR);
		has_early_error=true;
		return false;
	}

	if(!startFileMetadataDownloadThread())
	{
		ServerLogger::Log(logid, "Error starting file metadata download thread", LL_ERROR);
		has_early_error=true;
		return false;
	}

	_i64 filelist_size=tmp_filelist->Size();

	char buffer[4096];
	_u32 read;
	std::string curr_path;
	std::string curr_os_path;
	std::string curr_orig_path;
	std::string orig_sep;
	SFile cf;
	int depth=0;
	int64 laststatsupdate=0;
	int64 last_eta_update=0;
	int64 last_eta_received_bytes=0;
	double eta_estimated_speed=0;
	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, false);
	Server->getThreadPool()->execute(running_updater, "backup progress update");

	ServerLogger::Log(logid, clientname+": Started loading files...", LL_INFO);

	bool with_sparse_hashing = client_main->getProtocolVersions().select_sha_version > 0;

	std::vector<size_t> diffs;
	bool backup_with_components;
	_i64 files_size = getIncrementalSize(tmp_filelist, diffs, backup_with_components, true);

	std::string last_backuppath;
	std::string last_backuppath_complete;
	std::auto_ptr<ServerDownloadThreadGroup> server_download(new ServerDownloadThreadGroup(fc, NULL, backuppath,
		backuppath_hashes, last_backuppath, last_backuppath_complete,
		hashed_transfer, save_incomplete_files, clientid, clientname, clientsubname,
		use_tmpfiles, tmpfile_path, server_token, use_reflink,
		backupid, false, hashpipe_prepare, client_main, client_main->getProtocolVersions().filesrv_protocol_version,
		0, logid, with_hashes, shares_without_snapshot, with_sparse_hashing, metadata_download_thread.get(),
		backup_with_components, server_settings->getSettings()->download_threads, server_settings.get(),
		false, filepath_corrections, max_file_id));

	bool queue_downloads = client_main->getProtocolVersions().filesrv_protocol_version>2;

	ServerStatus::setProcessTotalBytes(clientname, status_id, files_size);

	fc.resetReceivedDataBytes(true);
	tmp_filelist->Seek(0);

	size_t line = 0;
	int64 linked_bytes = 0;

	size_t max_ok_id=0;

	bool c_has_error=false;
	bool script_dir=false;
	bool r_offline=false;

	std::stack<std::set<std::string> > folder_files;
	folder_files.push(std::set<std::string>());
	std::vector<size_t> folder_items;
	folder_items.push_back(0);

	bool has_read_error = false;
	while( (read=tmp_filelist->Read(buffer, 4096, &has_read_error))>0 && !r_offline && !c_has_error)
	{
		if (has_read_error)
		{
			break;
		}

		for(size_t i=0;i<read;++i)
		{
			std::map<std::string, std::string> extra_params;
			bool b=list_parser.nextEntry(buffer[i], cf, &extra_params);
			if(b)
			{
				FileMetadata metadata;
				metadata.read(extra_params);

				bool has_orig_path = metadata.has_orig_path;
				if(has_orig_path)
				{
					curr_orig_path = metadata.orig_path;
					str_map::iterator it_orig_sep = extra_params.find("orig_sep");
					if(it_orig_sep!=extra_params.end())
					{
						orig_sep = it_orig_sep->second;
					}
					if(orig_sep.empty()) orig_sep="\\";
				}

				do
				{
					int64 ctime = Server->getTimeMS();
					if (ctime - laststatsupdate > status_update_intervall)
					{
						if (ServerStatus::getProcess(clientname, status_id).stop)
						{
							r_offline = true;
							should_backoff = false;
							ServerLogger::Log(logid, "Server admin stopped backup.", LL_ERROR);
							server_download->queueSkip();
							break;
						}

						laststatsupdate = ctime;
						if (files_size == 0)
						{
							ServerStatus::setProcessPcDone(clientname, status_id, 100);
						}
						else
						{
							int64 done_bytes = fc.getReceivedDataBytes(true) + linked_bytes;
							ServerStatus::setProcessDoneBytes(clientname, status_id, done_bytes);
							ServerStatus::setProcessPcDone(clientname, status_id,
								(std::min)(100, (int)(((float)done_bytes) / ((float)files_size / 100.f) + 0.5f)));
						}

						ServerStatus::setProcessQueuesize(clientname, status_id,
							(_u32)hashpipe->getNumElements(), (_u32)hashpipe_prepare->getNumElements());
					}

					if (ctime - last_eta_update > eta_update_intervall)
					{
						calculateEtaFileBackup(last_eta_update, eta_set_time, ctime, fc, NULL, linked_bytes, last_eta_received_bytes, eta_estimated_speed, files_size);
					}

					calculateDownloadSpeed(ctime, fc, NULL);

				} while (server_download->sleepQueue());

				if(server_download->isOffline())
				{
					ServerLogger::Log(logid, "Client "+clientname+" went offline.", LL_ERROR);
					r_offline=true;
					break;
				}

				std::string osspecific_name;

				if(!cf.isdir || cf.name!="..")
				{
					osspecific_name = fixFilenameForOS(cf.name, folder_files.top(), curr_path, true, logid, filepath_corrections);

					for(size_t j=0;j<folder_items.size();++j)
					{
						++folder_items[j];
					}
				}

				if(cf.isdir)
				{
					if(cf.name!="..")
					{
						std::string orig_curr_path = curr_path;
						std::string orig_curr_os_path = curr_os_path;
						curr_path+="/"+cf.name;
						curr_os_path+="/"+osspecific_name;
						std::string local_curr_os_path=convertToOSPathFromFileClient(curr_os_path);

						if(!has_orig_path)
						{
							if (curr_orig_path != orig_sep)
							{
								curr_orig_path += orig_sep;
							}
							curr_orig_path += cf.name;
							metadata.orig_path = curr_orig_path;
                            metadata.exist=true;
							metadata.has_orig_path=true;
						}

						std::string metadata_fn = backuppath_hashes+local_curr_os_path+os_file_sep()+metadata_dir_fn;

						bool create_hash_dir=true;
						str_map::iterator sym_target = extra_params.find("sym_target");
						if(sym_target!=extra_params.end())
						{
							if(!createSymlink(backuppath+local_curr_os_path, depth, sym_target->second, (orig_sep), true))
							{
								ServerLogger::Log(logid, "Creating symlink at \""+backuppath+local_curr_os_path+"\" to \""+sym_target->second+"\" failed. " + systemErrorInfo(), LL_ERROR);
								c_has_error=true;
								break;
							}

							metadata_fn = backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + "/" + escape_metadata_fn(cf.name)); 
							create_hash_dir=false;
						}
						else if(!os_create_dir(os_file_prefix(backuppath+local_curr_os_path)))
						{
							ServerLogger::Log(logid, "Creating directory  \""+backuppath+local_curr_os_path+"\" failed. " + systemErrorInfo(), LL_ERROR);
							c_has_error=true;
							break;
						}

						if (depth == 0 && curr_path == "/urbackup_backup_scripts")
						{
							metadata.file_permissions = permissionsAllowAll();
							curr_orig_path = local_curr_os_path;
							metadata.orig_path = curr_orig_path;
						}
						
						if(create_hash_dir && !os_create_dir(os_file_prefix(backuppath_hashes+local_curr_os_path)))
						{
							ServerLogger::Log(logid, "Creating directory  \""+backuppath_hashes+local_curr_os_path+"\" failed. " + systemErrorInfo(), LL_ERROR);
							c_has_error=true;
							break;
						}
						else if(metadata.exist && !write_file_metadata(metadata_fn, client_main, metadata, false))
						{
							ServerLogger::Log(logid, "Writing directory metadata to \""+metadata_fn+"\" failed.", LL_ERROR);
							c_has_error=true;
							break;
						}

						folder_files.push(std::set<std::string>());
						folder_items.push_back(0);

						++depth;
						if(depth==1)
						{
							std::string t=curr_path;
							t.erase(0,1);
							if(t=="urbackup_backup_scripts")
							{
								script_dir=true;
							}
							else
							{
								ServerLogger::Log(logid, "Starting shadowcopy \""+t+"\".", LL_DEBUG);
								server_download->addToQueueStartShadowcopy(t);

								continuous_sequences[cf.name]=SContinuousSequence(
									watoi64(extra_params["sequence_id"]), watoi64(extra_params["sequence_next"]));
							}							
						}
					}
					else
					{
						folder_files.pop();

                        if(client_main->getProtocolVersions().file_meta>0 && !script_dir)
						{
							server_download->addToQueueFull(line, ExtractFileName(curr_path, "/"),
								ExtractFileName(curr_os_path, "/"), ExtractFilePath(curr_path, "/"), ExtractFilePath(curr_os_path, "/"), queue_downloads?0:-1,
								metadata, false, true, folder_items.back(), std::string());
						}
						folder_items.pop_back();
						--depth;
						if(depth==0)
						{
							std::string t=curr_path;
							t.erase(0,1);
							if(t=="urbackup_backup_scripts")
							{
								script_dir=false;
							}
							else
							{
								ServerLogger::Log(logid, "Stopping shadowcopy \""+t+"\".", LL_DEBUG);
								server_download->addToQueueStopShadowcopy(t);
							}							
						}

						curr_path=ExtractFilePath(curr_path, "/");
						curr_os_path=ExtractFilePath(curr_os_path, "/");

						if(!has_orig_path)
						{
							curr_orig_path = ExtractFilePath(curr_orig_path, orig_sep);
						}
					}
				}
				else
				{
					if (depth == 0)
					{
						server_download->addToQueueStartShadowcopy(cf.name);
					}

					if(!has_orig_path)
					{
						if (curr_orig_path != orig_sep)
						{
							metadata.orig_path = curr_orig_path + orig_sep + cf.name;
						}
						else
						{
							metadata.orig_path = orig_sep + cf.name;
						}
					}

					bool file_ok=false;
					bool write_file_metadata = false;

                    str_map::iterator sym_target = extra_params.find("sym_target");
                    if(sym_target!=extra_params.end())
                    {
                        std::string symlink_path = backuppath + convertToOSPathFromFileClient(curr_os_path)+os_file_sep()+osspecific_name;
                        if(!createSymlink(symlink_path, depth, sym_target->second, (orig_sep), false))
                        {
                            ServerLogger::Log(logid, "Creating symlink at \""+symlink_path+"\" to \""+sym_target->second+"\" failed. " + systemErrorInfo(), LL_ERROR);
                            c_has_error=true;
                            break;
                        }
                        else
                        {
							if (line>max_ok_id)
							{
								max_ok_id = line;
							}

                            file_ok=true;
							write_file_metadata = true;
                        }
                    }
					else if(extra_params.find("special")!=extra_params.end())
					{
						std::string touch_path = backuppath + convertToOSPathFromFileClient(curr_os_path)+os_file_sep()+osspecific_name;
						std::auto_ptr<IFile> touch_file(Server->openFile(os_file_prefix(touch_path), MODE_WRITE));
						if(touch_file.get()==NULL)
						{
							ServerLogger::Log(logid, "Error touching file at \""+touch_path+"\". " + systemErrorInfo(), LL_ERROR);
							c_has_error=true;
							break;
						}
						else
						{
							if (line>max_ok_id)
							{
								max_ok_id = line;
							}

							file_ok=true;
							write_file_metadata = true;
						}
					}

					std::string curr_sha2;
					std::map<std::string, std::string>::iterator hash_it=( (local_hash.get()==NULL)?extra_params.end():extra_params.find(sha_def_identifier) );
					
					if (local_hash.get() != NULL && hash_it == extra_params.end())
					{
						hash_it = extra_params.find("thash");
					}

					if (!file_ok
						&& hash_it != extra_params.end())
					{
						curr_sha2 = base64_decode_dash(hash_it->second);
					}
					else if (!file_ok
						&& phash_load.get() != NULL
						&& !script_dir
						&& extra_params.find("no_hash") == extra_params.end()
						&& cf.size >= link_file_min_size)
					{
						if (!phash_load->getHash(line, curr_sha2))
						{
							ServerLogger::Log(logid, "Error getting parallel hash for file \"" + cf.name + "\" line " + convert(line), LL_ERROR);
							r_offline = true;
							break;
						}
						else
						{
							metadata.shahash = curr_sha2;
						}
					}

					if(!curr_sha2.empty())
					{						
						if(cf.size>= link_file_min_size
							&& link_file(cf.name, osspecific_name, curr_path, curr_os_path, curr_sha2, cf.size,
							             true, metadata))
						{
							file_ok=true;
							linked_bytes+=cf.size;
							if(line>max_ok_id)
							{
								max_ok_id=line;
							}
						}
					}

                    if(file_ok)
                    {
						if(client_main->getProtocolVersions().file_meta>0)
						{
                    	    server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?0:-1,
                    	        metadata, script_dir, true, 0, curr_sha2, false, 0, std::string(), write_file_metadata);
                    	}
                    }
                    else
					{
						server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
							metadata, script_dir, false, 0, curr_sha2);
					}

					if (depth == 0)
					{
						server_download->addToQueueStopShadowcopy(cf.name);
					}
				}

				max_file_id.setMaxPreProcessed(line);
				++line;
			}
		}

		if(read<4096)
			break;
	}

	if (has_read_error)
	{
		ServerLogger::Log(logid, "Error reading from file " + tmp_filelist->getFilename() + ". " + os_last_error_str(), LL_ERROR);
		disk_error = true;
	}

	stopPhashDownloadThread(filelist_async_id);


	server_download->queueStop();

	ServerLogger::Log(logid, "Waiting for file transfers...", LL_INFO);

	while(!server_download->join(1000))
	{
		if(files_size==0)
		{
			ServerStatus::setProcessPcDone(clientname, status_id, 100);
		}
		else
		{
			int64 done_bytes = fc.getReceivedDataBytes(true) + linked_bytes;
			ServerStatus::setProcessDoneBytes(clientname, status_id, done_bytes);
			ServerStatus::setProcessPcDone(clientname, status_id,
				(std::min)(100,(int)(((float)done_bytes)/((float)files_size/100.f)+0.5f)));
		}

		ServerStatus::setProcessQueuesize(clientname, status_id,
			(_u32)hashpipe->getNumElements(), (_u32)hashpipe_prepare->getNumElements());

		int64 ctime = Server->getTimeMS();
		if(ctime-last_eta_update>eta_update_intervall)
		{
			calculateEtaFileBackup(last_eta_update, eta_set_time, ctime, fc, NULL, linked_bytes, last_eta_received_bytes, eta_estimated_speed, files_size);
		}

		calculateDownloadSpeed(ctime, fc, NULL);
	}

	ServerStatus::setProcessSpeed(clientname, status_id, 0);

	if(server_download->isOffline() && !r_offline)
	{
		ServerLogger::Log(logid, "Client "+clientname+" went offline.", LL_ERROR);
		r_offline=true;
	}

	num_issues += server_download->getNumIssues();

	if (server_download->getHasDiskError()
		&& !server_settings->getSettings()->ignore_disk_errors)
	{
		r_offline = true;
	}

	int64 transfer_stop_time = Server->getTimeMS();

	size_t max_line = line;

	if(!r_offline && !c_has_error && !disk_error)
	{
		sendBackupOkay(true);
	}
	else
	{
		sendBackupOkay(false);
	}

	if(r_offline && server_download->hasTimeout() && !server_download->shouldBackoff())
	{
		ServerLogger::Log(logid, "Client had timeout. Retrying backup soon...", LL_INFO);
		should_backoff=false;
		has_timeout_error=true;
	}

	running_updater->stop();
	backup_dao->updateFileBackupRunning(backupid);

	ServerLogger::Log(logid, "Waiting for file hashing and copying threads...", LL_INFO);

	waitForFileThreads();

	if (!r_offline && !c_has_error && !disk_error)
	{
		if (!loadWindowsBackupComponentConfigXml(fc))
		{
			c_has_error = true;
		}
	}

	stopFileMetadataDownloadThread(false, server_download->getNumEmbeddedMetadataFiles());

	if (!r_offline && !c_has_error && !disk_error
		&& client_main->getProtocolVersions().wtokens_version > 0 )
	{
		getTokenFile(fc, hashed_transfer, true);
	}

	server_download->deleteTempFolder();

	ServerLogger::Log(logid, "Writing new file list...", LL_INFO);

	bool has_all_metadata=true;

	tmp_filelist->Seek(0);
	line = 0;
	list_parser.reset();
	size_t output_offset=0;
	std::stack<size_t> last_modified_offsets;
	script_dir=false;
	has_read_error = false;
	size_t download_max_ok_id = server_download->getMaxOkId();
	while( (read=tmp_filelist->Read(buffer, 4096, &has_read_error))>0 )
	{
		if (has_read_error)
		{
			break;
		}

		for(size_t i=0;i<read;++i)
		{
			bool b=list_parser.nextEntry(buffer[i], cf, NULL);
			if(b)
			{
				if(cf.isdir)
				{
					if(cf.name!="..")
					{
						if (line < max_line)
						{
							size_t curr_last_modified_offset = 0;
							size_t curr_output_offset = output_offset;
							writeFileItem(clientlist, cf, &output_offset, &curr_last_modified_offset);

							last_modified_offsets.push(curr_output_offset + curr_last_modified_offset);
						}
						else
						{
							last_modified_offsets.push(std::string::npos);
						}

						if(cf.name=="urbackup_backup_scripts")
						{
							script_dir=true;
						}
					}					
					else
					{
						if(!script_dir
							&& metadata_download_thread.get()!=NULL
							&& !metadata_download_thread->hasMetadataId(line+1)
							&& last_modified_offsets.top()!= std::string::npos)
						{
							if (line < max_line)
							{
								has_all_metadata = false;
								ServerLogger::Log(logid, "Metadata of \"" + cf.name + "\" missing", LL_DEBUG);
							}

							//go back to the directory entry and change the last modified time
							if(clientlist->Seek(last_modified_offsets.top()))
							{
								char ch;
								if(clientlist->Read(&ch, 1)==1)
								{
									ch = (ch=='0' ? '1' : '0');
									if(!clientlist->Seek(last_modified_offsets.top())
										|| clientlist->Write(&ch, 1)!=1)
									{
										ServerLogger::Log(logid, "Error writing to clientlist", LL_ERROR);
									}
								}
								else
								{
									ServerLogger::Log(logid, "Error reading from clientlist "+clientlist->getFilename()+" from offset "+convert(last_modified_offsets.top()), LL_ERROR);	
								}
							}
							else
							{
								ServerLogger::Log(logid, "Error seeking in clientlist", LL_ERROR);	
							}

							if(!clientlist->Seek(clientlist->Size()))
							{
								ServerLogger::Log(logid, "Error seeking to end in clientlist", LL_ERROR);	
							}
						}

						if (line < max_line)
						{
							writeFileItem(clientlist, cf, &output_offset);
						}

						script_dir=false;
						last_modified_offsets.pop();
					}					
				}
				else if(!cf.isdir && 
					line <= (std::max)(download_max_ok_id, max_ok_id) &&
					server_download->isDownloadOk(line) )
				{
					bool metadata_missing = (!script_dir && metadata_download_thread.get()!=NULL
						&& !metadata_download_thread->hasMetadataId(line+1));

					if(metadata_missing)
					{
						ServerLogger::Log(logid, "Metadata of \"" + cf.name + "\" is missing", LL_DEBUG);

						has_all_metadata=false;
					}

					if(server_download->isDownloadPartial(line)
						|| metadata_missing)
					{
						if(cf.last_modified==0)
						{
							cf.last_modified+=1;
						}
						cf.last_modified *= Server->getRandomNumber();
					}
					writeFileItem(clientlist, cf, &output_offset);
				}				
				++line;
			}
		}
	}

	if (has_read_error)
	{
		ServerLogger::Log(logid, "Error reading from file " + tmp_filelist->getFilename() + ". " + os_last_error_str(), LL_ERROR);
		disk_error = true;
	}

	if(has_all_metadata)
	{
		ServerLogger::Log(logid, "All metadata was present", LL_INFO);
	}
	else
	{
		ServerLogger::Log(logid, "Some metadata was missing", LL_DEBUG);
	}

	bool verification_ok = true;
	if(!r_offline && !c_has_error
	        && (server_settings->getSettings()->end_to_end_file_backup_verification
		|| (client_main->isOnInternetConnection()
		&& server_settings->getSettings()->verify_using_client_hashes 
		&& server_settings->getSettings()->internet_calculate_filehashes_on_client) ) )
	{
		if(!verify_file_backup(tmp_filelist))
		{
			ServerLogger::Log(logid, "Backup verification failed", LL_ERROR);
			c_has_error=true;
			verification_ok = false;
		}
		else
		{
			ServerLogger::Log(logid, "Backup verification ok", LL_INFO);
		}
	}

	for (size_t i = 0; i < bsh.size(); ++i)
	{
		if (bsh[i]->hasError())
			disk_error = true;
	}
	for (size_t i = 0; i < bsh_prepare.size(); ++i)
	{
		if (bsh_prepare[i]->hasError())
			disk_error = true;
	}

	if(!disk_error && verification_ok)
	{
		FileIndex::flush();

		ServerLogger::Log(logid, "Syncing file system...", LL_DEBUG);

		clientlist->Sync();
		clientlist_delete.release();

		if ( !os_sync(backuppath)
			|| !os_sync(backuppath_hashes) )
		{
			ServerLogger::Log(logid, "Syncing file system failed. Backup may not be completely on disk. " + os_last_error_str(), BackupServer::canSyncFs() ? LL_ERROR : LL_DEBUG);

			if(BackupServer::canSyncFs())
				c_has_error = true;
		}

		std::auto_ptr<IFile> sync_f;
		if (!c_has_error)
		{
			sync_f.reset(Server->openFile(os_file_prefix(backuppath_hashes + os_file_sep() + sync_fn), MODE_WRITE));

			if (sync_f.get() != NULL
				&& !sync_f->Sync())
			{
				ServerLogger::Log(logid, "Error syncing sync file to disk. " + os_last_error_str(), LL_ERROR);
				c_has_error = true;
			}
		}

		if (!c_has_error
			&& use_snapshots)
		{
			if (!SnapshotHelper::makeReadonly(false, clientname, backuppath_single))
			{
				ServerLogger::Log(logid, "Making backup snapshot read only failed", LL_WARNING);
			}
		}

		if (sync_f.get() != NULL)
		{
			DBScopedSynchronous synchronous(db);
			DBScopedWriteTransaction trans(db);

			backup_dao->setFileBackupDone(backupid);
			backup_dao->setFileBackupSynced(backupid);
		}
		else if(!c_has_error)
		{
			ServerLogger::Log(logid, "Error creating sync file at "+ backuppath_hashes + os_file_sep() + sync_fn+". Not setting backup to done.", LL_ERROR);
			c_has_error = true;
		}

		Server->destroy(clientlist);
	}

	if( !r_offline && !c_has_error && !disk_error
		&& (group==c_group_default || group==c_group_continuous)) 
	{
		std::string backupfolder=server_settings->getSettings()->backupfolder;

		std::string name="current";
		if(group==c_group_continuous)
		{
			name="continuous";
		}

		std::string currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+name;
		os_remove_symlink_dir(os_file_prefix(currdir));
		os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

		if(group==c_group_default)
		{
			currdir=backupfolder+os_file_sep()+"clients";
			if(!os_create_dir(os_file_prefix(currdir)) && !os_directory_exists(os_file_prefix(currdir)))
			{
				Server->Log("Error creating \"clients\" dir for symbolic links", LL_ERROR);
			}
			currdir+=os_file_sep()+clientname;
			os_remove_symlink_dir(os_file_prefix(currdir));
			os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

			if(server_settings->getSettings()->create_linked_user_views)
			{
				ServerLogger::Log(logid, "Creating user views...", LL_INFO);

				createUserViews(tmp_filelist);
			}

			saveUsersOnClient();
		}
	}

	_i64 transferred_bytes=fc.getTransferredBytes();
	_i64 transferred_compressed=fc.getRealTransferredBytes();
	int64 passed_time=transfer_stop_time-full_backup_starttime;
	if(passed_time==0) passed_time=1;

	ServerLogger::Log(logid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );
	if(transferred_compressed>0)
	{
		ServerLogger::Log(logid, "(Before compression: "+PrettyPrintBytes(transferred_compressed)+" ratio: "+convert((float)transferred_compressed/transferred_bytes)+")");
	}
	if (linked_bytes > 0)
	{
		ServerLogger::Log(logid, PrettyPrintBytes(linked_bytes) + " of files were already present on the server and did not need to be transferred", LL_INFO);
	}

	if (!ClientMain::run_script("urbackup" + os_file_sep() + "post_full_filebackup",
		"\"" + backuppath + "\" " + ((c_has_error || r_offline || disk_error) ? "0" : "1") + " " + convert(group), logid))
	{
		c_has_error = true;
	}

	if(c_has_error)
		return false;

	return !r_offline;
}

