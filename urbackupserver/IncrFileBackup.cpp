/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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
#include "IncrFileBackup.h"
#include "server_log.h"
#include "snapshot_helper.h"
#include "../urbackupcommon/os_functions.h"
#include "ClientMain.h"
#include "treediff/TreeDiff.h"
#include "../urbackupcommon/filelist_utils.h"
#include "server_dir_links.h"
#include "server_running.h"
#include "ServerDownloadThread.h"
#include "server_hash_existing.h"
#include "FileIndex.h"
#include <stack>
#include "../urbackupcommon/file_metadata.h"
#include "server.h"
#include "../common/adler32.h"
#include "../common/data.h"
#include "FullFileBackup.h"
#include <algorithm>

extern std::string server_identity;
extern std::string server_token;

const int64 c_readd_size_limit=100*1024;

IncrFileBackup::IncrFileBackup( ClientMain* client_main, int clientid, std::wstring clientname, LogAction log_action,
	int group, bool use_tmpfiles, std::wstring tmpfile_path, bool use_reflink, bool use_snapshots )
	: FileBackup(client_main, clientid, clientname, log_action, true, group, use_tmpfiles, tmpfile_path, use_reflink, use_snapshots), 
	intra_file_diffs(intra_file_diffs), hash_existing_mutex(NULL)
{

}

bool IncrFileBackup::doFileBackup()
{
	ServerLogger::Log(logid, "Starting incremental file backup...", LL_INFO);

	if(with_hashes)
	{
		ServerLogger::Log(logid, clientname+L": Doing backup with hashes...", LL_DEBUG);
	}

	bool intra_file_diffs;
	if(client_main->isOnInternetConnection())
	{
		intra_file_diffs=(server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash");
	}
	else
	{
		intra_file_diffs=(server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash");
	}

	if(intra_file_diffs)
	{
		ServerLogger::Log(logid, clientname+L": Doing backup with intra file diffs...", LL_DEBUG);
	}

	bool use_directory_links = !use_snapshots && server_settings->getSettings()->use_incremental_symlinks;

	SBackup last=getLastIncremental(group);
	if(last.incremental==-2)
	{
		ServerLogger::Log(logid, "Cannot retrieve last file backup when doing incremental backup. Doing full backup now...", LL_WARNING);

		deleteBackup();

		return doFullBackup();
	}

	int64 eta_set_time=Server->getTimeMS();
	ServerStatus::setProcessEta(clientname, status_id, 
		last.backup_time_ms + last.indexing_time_ms,
		eta_set_time);


	int64 indexing_start_time = Server->getTimeMS();
	bool resumed_backup = !last.is_complete;
	bool resumed_full = (resumed_backup && last.incremental==0);

	if(resumed_backup)
	{
		r_resumed=true;

		if(resumed_full)
		{
			r_incremental=false;	
		}
	}

	bool no_backup_dirs=false;
	bool connect_fail = false;
	bool b=request_filelist_construct(resumed_full, resumed_backup, group, true, no_backup_dirs, connect_fail);
	if(!b)
	{
		has_early_error=true;

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

	if(client_main->isOnInternetConnection())
	{
		if(server_settings->getSettings()->internet_incr_file_transfer_mode=="raw")
			hashed_transfer=false;
	}
	else
	{
		if(server_settings->getSettings()->local_incr_file_transfer_mode=="raw")
			hashed_transfer=false;
	}

	if(hashed_transfer)
	{
		ServerLogger::Log(logid, clientname+L": Doing backup with hashed transfer...", LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(logid, clientname+L": Doing backup without hashed transfer...", LL_DEBUG);
	}

	Server->Log(clientname+L": Connecting to client...", LL_DEBUG);
	std::string identity = client_main->getSessionIdentity().empty()?server_identity:client_main->getSessionIdentity();
	FileClient fc(false, identity, client_main->getProtocolVersions().filesrv_protocol_version, client_main->isOnInternetConnection(), client_main, use_tmpfiles?NULL:client_main);
	std::auto_ptr<FileClientChunked> fc_chunked;
	if(intra_file_diffs)
	{
		if(client_main->getClientChunkedFilesrvConnection(fc_chunked, server_settings.get(), 10000))
		{
			fc_chunked->setDestroyPipe(true);
			if(fc_chunked->hasError())
			{
				ServerLogger::Log(logid, L"Incremental Backup of "+clientname+L" failed - CONNECT error -1", LL_ERROR);
				has_early_error=true;
				log_backup=false;
				return false;
			}
		}
		else
		{
			ServerLogger::Log(logid, L"Incremental Backup of "+clientname+L" failed - CONNECT error -3", LL_ERROR);
			has_early_error=true;
			log_backup=false;
			return false;
		}
	}
	_u32 rc=client_main->getClientFilesrvConnection(&fc, server_settings.get(), 10000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(logid, L"Incremental Backup of "+clientname+L" failed - CONNECT error -2", LL_ERROR);
		has_early_error=true;
		log_backup=false;
		return false;
	}

	ServerLogger::Log(logid, clientname+L": Loading file list...", LL_INFO);
	IFile *tmp=ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
	if(tmp==NULL)
	{
		ServerLogger::Log(logid, L"Error creating temporary file in ::doIncrBackup", LL_ERROR);
		return false;
	}

	int64 incr_backup_starttime=Server->getTimeMS();
	int64 incr_backup_stoptime=0;

	rc=fc.GetFile(group>0?("urbackup/filelist_"+nconvert(group)+".ub"):"urbackup/filelist.ub", tmp, hashed_transfer, false);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, L"Error getting filelist of "+clientname+L". Errorcode: "+widen(fc.getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		has_early_error=true;
		return false;
	}

	ServerLogger::Log(logid, clientname+L" Starting incremental backup...", LL_DEBUG);

	int incremental_num = resumed_full?0:(last.incremental+1);
	backup_dao->newFileBackup(incremental_num, clientid, backuppath_single, resumed_backup, Server->getTimeMS()-indexing_start_time, group);
	backupid=static_cast<int>(db->getLastInsertID());

	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	std::wstring last_backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path;
	std::wstring last_backuppath_hashes=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path+os_file_sep()+L".hashes";
	std::wstring last_backuppath_complete=backupfolder+os_file_sep()+clientname+os_file_sep()+last.complete;

	std::wstring tmpfilename=tmp->getFilenameW();
	Server->destroy(tmp);

	ServerLogger::Log(logid, clientname+L": Calculating file tree differences...", LL_INFO);

	bool error=false;
	std::vector<size_t> deleted_ids;
	std::vector<size_t> *deleted_ids_ref=NULL;
	if(use_snapshots) deleted_ids_ref=&deleted_ids;
	std::vector<size_t> large_unchanged_subtrees;
	std::vector<size_t> *large_unchanged_subtrees_ref=NULL;
	if(use_directory_links) large_unchanged_subtrees_ref=&large_unchanged_subtrees;
	std::vector<size_t> modified_inplace_ids;

	std::vector<size_t> diffs=TreeDiff::diffTrees(clientlistName(group), wnarrow(tmpfilename),
		error, deleted_ids_ref, large_unchanged_subtrees_ref, &modified_inplace_ids);

	if(error)
	{
		if(!client_main->isOnInternetConnection())
		{
			ServerLogger::Log(logid, "Error while calculating tree diff. Doing full backup.", LL_ERROR);
			return doFullBackup();
		}
		else
		{
			ServerLogger::Log(logid, "Error while calculating tree diff. Not doing full backup because of internet connection.", LL_ERROR);
			has_early_error=true;
			return false;
		}
	}

	if(use_snapshots)
	{
		ServerLogger::Log(logid, clientname+L": Creating snapshot...", LL_INFO);
		if(!SnapshotHelper::snapshotFileSystem(clientname, last.path, backuppath_single)
			|| !SnapshotHelper::isSubvolume(clientname, backuppath_single) )
		{
			ServerLogger::Log(logid, "Creating new snapshot failed (Server error)", LL_WARNING);

			if(!SnapshotHelper::createEmptyFilesystem(clientname, backuppath_single) )
			{
				ServerLogger::Log(logid, "Creating empty filesystem failed (Server error)", LL_ERROR);
				has_early_error=true;
				return false;
			}
			if(with_hashes)
			{
				if(!os_create_dir(os_file_prefix(backuppath_hashes)) )
				{
					ServerLogger::Log(logid, "Cannot create hash path (Server error)", LL_ERROR);
					has_early_error=true;
					return false;
				}
			}

			use_snapshots=false;
		}
	}

	getTokenFile(fc, hashed_transfer);

	if(use_snapshots)
	{
		ServerLogger::Log(logid, clientname+L": Deleting files in snapshot... ("+convert(deleted_ids.size())+L")", LL_INFO);
		if(!deleteFilesInSnapshot(clientlistName(group), deleted_ids, backuppath, false) )
		{
			ServerLogger::Log(logid, "Deleting files in snapshot failed (Server error)", LL_ERROR);
			has_early_error=true;
			return false;
		}

		if(with_hashes)
		{
			ServerLogger::Log(logid, clientname+L": Deleting files in hash snapshot...", LL_INFO);
			deleteFilesInSnapshot(clientlistName(group), deleted_ids, backuppath_hashes, true);
		}
	}

	if(!startFileMetadataDownloadThread())
	{
		ServerLogger::Log(logid, "Error starting file metadata download thread", LL_ERROR);
		has_early_error=true;
		return false;
	}

	bool readd_file_entries_sparse = client_main->isOnInternetConnection() && server_settings->getSettings()->internet_calculate_filehashes_on_client
		&& server_settings->getSettings()->internet_readd_file_entries;

	size_t num_readded_entries = 0;

	bool copy_last_file_entries = resumed_backup;

	size_t num_copied_file_entries = 0;

	int copy_file_entries_sparse_modulo = server_settings->getSettings()->min_file_incr;

	bool trust_client_hashes = server_settings->getSettings()->trust_client_hashes;

	if(copy_last_file_entries)
	{
		copy_last_file_entries = copy_last_file_entries && backup_dao->createTemporaryLastFilesTable();
		backup_dao->createTemporaryLastFilesTableIndex();
		copy_last_file_entries = copy_last_file_entries && backup_dao->copyToTemporaryLastFilesTable(last.backupid);

		if(resumed_full)
		{
			readd_file_entries_sparse=false;
		}
	}

	IFile *clientlist=Server->openFile(clientlistName(group, true), MODE_WRITE);

	tmp=Server->openFile(tmpfilename, MODE_READ);

	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, false);
	Server->getThreadPool()->execute(running_updater);

	std::auto_ptr<ServerDownloadThread> server_download(new ServerDownloadThread(fc, fc_chunked.get(), backuppath,
		backuppath_hashes, last_backuppath, last_backuppath_complete,
		hashed_transfer, intra_file_diffs, clientid, clientname,
		use_tmpfiles, tmpfile_path, server_token, use_reflink,
		backupid, r_incremental, hashpipe_prepare, client_main, client_main->getProtocolVersions().filesrv_protocol_version,
		incremental_num, logid));

	bool queue_downloads = client_main->getProtocolVersions().filesrv_protocol_version>2;

	THREADPOOL_TICKET server_download_ticket = 
		Server->getThreadPool()->execute(server_download.get());

	std::auto_ptr<ServerHashExisting> server_hash_existing;
	THREADPOOL_TICKET server_hash_existing_ticket = ILLEGAL_THREADPOOL_TICKET;
	if(readd_file_entries_sparse && !trust_client_hashes)
	{
		server_hash_existing.reset(new ServerHashExisting(clientid, logid, this));
		server_hash_existing_ticket =
			Server->getThreadPool()->execute(server_hash_existing.get());
	}

	char buffer[4096];
	_u32 read;
	std::wstring curr_path;
	std::wstring curr_os_path;
	std::wstring curr_hash_path;
	std::string curr_orig_path;
	std::string orig_sep;
	SFile cf;
	int depth=0;
	int line=0;
	int link_logcnt=0;
	bool indirchange=false;
	int changelevel;
	bool r_offline=false;
	_i64 filelist_size=tmp->Size();
	_i64 filelist_currpos=0;
	int indir_currdepth=0;
	IdRange download_nok_ids;

	fc.resetReceivedDataBytes();
	if(fc_chunked.get()!=NULL)
	{
		fc_chunked->resetReceivedDataBytes();
	}

	ServerLogger::Log(logid, clientname+L": Calculating tree difference size...", LL_INFO);
	_i64 files_size=getIncrementalSize(tmp, diffs);
	tmp->Seek(0);

	int64 laststatsupdate=0;
	int64 last_eta_update=0;
	int64 last_eta_received_bytes=0;
	double eta_estimated_speed=0;

	int64 linked_bytes = 0;

	ServerLogger::Log(logid, clientname+L": Linking unchanged and loading new files...", LL_INFO);

	FileListParser list_parser;

	bool c_has_error=false;
	bool backup_stopped=false;
	size_t skip_dir_completely=0;
	bool skip_dir_copy_sparse=false;
	bool script_dir=false;

	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		filelist_currpos+=read;

		for(size_t i=0;i<read;++i)
		{
			std::map<std::wstring, std::wstring> extra_params;
			bool b=list_parser.nextEntry(buffer[i], cf, &extra_params);
			if(b)
			{
				std::wstring osspecific_name=fixFilenameForOS(cf.name);

				if(skip_dir_completely>0)
				{
					if(cf.isdir)
					{						
						if(cf.name==L"..")
						{
							--skip_dir_completely;
							if(skip_dir_completely>0)
							{
								curr_os_path=ExtractFilePath(curr_os_path, L"/");
								curr_path=ExtractFilePath(curr_path, L"/");
							}
						}
						else
						{
							curr_os_path+=L"/"+osspecific_name;
							curr_path+=L"/"+cf.name;
							++skip_dir_completely;
						}
					}
					else if(skip_dir_copy_sparse)
					{
						std::string curr_sha2;
						{
							std::map<std::wstring, std::wstring>::iterator hash_it = 
								( (local_hash.get()==NULL)?extra_params.end():extra_params.find(L"sha512") );					
							if(hash_it!=extra_params.end())
							{
								curr_sha2 = base64_decode_dash(wnarrow(hash_it->second));
							}
						}
						std::wstring local_curr_os_path=convertToOSPathFromFileClient(curr_os_path+L"/"+osspecific_name);
						addSparseFileEntry(curr_path, cf, copy_file_entries_sparse_modulo, incremental_num, trust_client_hashes,
							curr_sha2, local_curr_os_path, with_hashes, server_hash_existing, num_readded_entries);
					}


					if(skip_dir_completely>0)
					{
						++line;
						continue;
					}
				}

				FileMetadata metadata;
				metadata.read(extra_params);

				bool has_orig_path = metadata.has_orig_path;
				if(has_orig_path)
				{
					curr_orig_path = metadata.orig_path;
					orig_sep = base64_decode_dash(Server->ConvertToUTF8(extra_params[L"orig_sep"]));
					if(orig_sep.empty()) orig_sep="\\";
				}

				int64 ctime=Server->getTimeMS();
				if(ctime-laststatsupdate>status_update_intervall)
				{
					if(!backup_stopped)
					{
						if(ServerStatus::getProcess(clientname, status_id).stop)
						{
							r_offline=true;
							backup_stopped=true;
							ServerLogger::Log(logid, L"Server admin stopped backup.", LL_ERROR);
							server_download->queueSkip();
							if(server_hash_existing.get())
							{
								server_hash_existing->queueStop(true);
							}
						}
					}

					laststatsupdate=ctime;
					if(files_size==0)
					{
						ServerStatus::setProcessPcDone(clientname, status_id, 100);
					}
					else
					{
						ServerStatus::setProcessPcDone(clientname, status_id,
								(std::min)(100,(int)(((float)(fc.getReceivedDataBytes()
									+ (fc_chunked.get()?fc_chunked->getReceivedDataBytes():0) + linked_bytes))/((float)files_size/100.f)+0.5f)) );
					}

					ServerStatus::setProcessQueuesize(clientname, status_id,
						(_u32)hashpipe->getNumElements(), (_u32)hashpipe_prepare->getNumElements());
				}

				if(ctime-last_eta_update>eta_update_intervall)
				{
					calculateEtaFileBackup(last_eta_update, eta_set_time, ctime, fc, fc_chunked.get(), linked_bytes, last_eta_received_bytes, eta_estimated_speed, files_size);
				}

				if(server_download->isOffline() && !r_offline)
				{
					ServerLogger::Log(logid, L"Client "+clientname+L" went offline.", LL_ERROR);
					r_offline=true;
					incr_backup_stoptime=Server->getTimeMS();
				}


				if(cf.isdir)
				{
					if(!indirchange && hasChange(line, diffs) )
					{
						indirchange=true;
						changelevel=depth;
						indir_currdepth=0;

						if(cf.name!=L"..")
						{
							indir_currdepth=1;
						}
						else
						{
							--changelevel;
						}
					}
					else if(indirchange)
					{
						if(cf.name!=L"..")
							++indir_currdepth;
						else
							--indir_currdepth;
					}

					if(cf.name!=L"..")
					{
						std::wstring orig_curr_path = curr_path;
						std::wstring orig_curr_os_path = curr_os_path;
						curr_path+=L"/"+cf.name;
						curr_os_path+=L"/"+osspecific_name;
						std::wstring local_curr_os_path=convertToOSPathFromFileClient(curr_os_path);

						if(!has_orig_path)
						{
							curr_orig_path += orig_sep + Server->ConvertToUTF8(cf.name);
							metadata.orig_path = curr_orig_path;
                            metadata.exist=true;
							metadata.has_orig_path=true;
						}

						std::wstring metadata_fn = backuppath_hashes+local_curr_os_path+os_file_sep()+metadata_dir_fn;

						bool dir_linked=false;
						if(use_directory_links && hasChange(line, large_unchanged_subtrees) )
						{
							std::wstring srcpath=last_backuppath+local_curr_os_path;
							if(link_directory_pool(*backup_dao, clientid, backuppath+local_curr_os_path,
								srcpath, dir_pool_path, BackupServer::isFilesystemTransactionEnabled()) )
							{
								skip_dir_completely=1;
								dir_linked=true;
								bool curr_has_hashes = false;

								std::wstring src_hashpath = last_backuppath_hashes+local_curr_os_path;

								if(with_hashes)
								{
									curr_has_hashes = link_directory_pool(*backup_dao, clientid, backuppath_hashes+local_curr_os_path,
										src_hashpath, dir_pool_path, BackupServer::isFilesystemTransactionEnabled());
								}

								if(copy_last_file_entries)
								{
									std::vector<ServerBackupDao::SFileEntry> file_entries = backup_dao->getFileEntriesFromTemporaryTableGlob(escape_glob_sql(srcpath)+os_file_sep()+L"*");
									for(size_t i=0;i<file_entries.size();++i)
									{
										if(file_entries[i].fullpath.size()>srcpath.size())
										{
											std::wstring entry_hashpath;
											if( curr_has_hashes && next(file_entries[i].hashpath, 0, src_hashpath))
											{
												entry_hashpath = backuppath_hashes+local_curr_os_path + file_entries[i].hashpath.substr(src_hashpath.size());
											}

											addFileEntrySQLWithExisting(backuppath + local_curr_os_path + file_entries[i].fullpath.substr(srcpath.size()), entry_hashpath,
												file_entries[i].shahash, file_entries[i].filesize, 0, incremental_num);

											++num_copied_file_entries;
										}
									}

									skip_dir_copy_sparse = false;
								}
								else
								{
									skip_dir_copy_sparse = readd_file_entries_sparse;
								}
							}
						}
						if(!dir_linked && (!use_snapshots || indirchange) )
						{
							bool create_hash_dir=true;
							str_map::iterator sym_target = extra_params.find(L"sym_target");
							if(sym_target!=extra_params.end())
							{
								if(!createSymlink(backuppath+local_curr_os_path, depth, sym_target->second, Server->ConvertToUnicode(orig_sep), true))
								{
									ServerLogger::Log(logid, L"Creating symlink at \""+backuppath+local_curr_os_path+L"\" to \""+sym_target->second+L" failed. " + widen(systemErrorInfo()), LL_ERROR);
									c_has_error=true;
									break;
								}

								metadata_fn = backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + L"/" + escape_metadata_fn(cf.name)); 
								create_hash_dir=false;
							}
							else if(!os_create_dir(os_file_prefix(backuppath+local_curr_os_path)))
							{
								if(!os_directory_exists(os_file_prefix(backuppath+local_curr_os_path)))
								{
									ServerLogger::Log(logid, L"Creating directory  \""+backuppath+local_curr_os_path+L"\" failed. - " + widen(systemErrorInfo()), LL_ERROR);
									c_has_error=true;
									break;
								}
								else
								{
									ServerLogger::Log(logid, L"Directory \""+backuppath+local_curr_os_path+L"\" does already exist.", LL_WARNING);
								}
							}
							
							if(create_hash_dir && !os_create_dir(os_file_prefix(backuppath_hashes+local_curr_os_path)))
							{
								if(!os_directory_exists(os_file_prefix(backuppath_hashes+local_curr_os_path)))
								{
									ServerLogger::Log(logid, L"Creating directory  \""+backuppath_hashes+local_curr_os_path+L"\" failed. - " + widen(systemErrorInfo()), LL_ERROR);
									c_has_error=true;
									break;
								}
								else
								{
									ServerLogger::Log(logid, L"Directory  \""+backuppath_hashes+local_curr_os_path+L"\" does already exist. - " + widen(systemErrorInfo()), LL_WARNING);
								}
							}
							else if(!write_file_metadata(metadata_fn, client_main, metadata, false) )
							{
								ServerLogger::Log(logid, L"Writing directory metadata to \""+backuppath_hashes+local_curr_os_path+os_file_sep()+metadata_dir_fn+L"\" failed.", LL_ERROR);
								c_has_error=true;
								break;
							}

                            if(!indirchange && curr_path!=L"/urbackup_backup_scripts")
							{
								std::wstring srcpath=last_backuppath_hashes+local_curr_os_path + os_file_sep()+metadata_dir_fn;
								if(!copy_os_metadata(srcpath, backuppath_hashes+local_curr_os_path+os_file_sep()+metadata_dir_fn, client_main))
								{
									ServerLogger::Log(logid, L"Error copying OS dependent directory metadata from last backup to \""+backuppath_hashes+local_curr_os_path+os_file_sep()+metadata_dir_fn+L"\".", LL_ERROR);
									c_has_error=true;
									break;
								}
							}
						}
						++depth;
						if(depth==1)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							if(t==L"urbackup_backup_scripts")
							{
								script_dir=true;
							}
							else
							{
								server_download->addToQueueStartShadowcopy(t);

								continuous_sequences[cf.name]=SContinuousSequence(
									watoi64(extra_params[L"sequence_id"]), watoi64(extra_params[L"sequence_next"]));
							}							
						}
					}
					else //cf.name==".."
					{
						if(indirchange && client_main->getProtocolVersions().file_meta>0)
						{
							server_download->addToQueueFull(line, ExtractFileName(curr_path, L"/"), ExtractFileName(curr_os_path, L"/"),
								ExtractFilePath(curr_path, L"/"), ExtractFilePath(curr_os_path, L"/"), queue_downloads?0:-1,
								metadata, false, true);
						}

						--depth;
						if(indirchange==true && depth==changelevel)
						{
							indirchange=false;
						}
						if(depth==0)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							if(t==L"urbackup_backup_scripts")
							{
								script_dir=false;
							}
							else
							{
								server_download->addToQueueStopShadowcopy(t);
							}							
						}
						curr_path=ExtractFilePath(curr_path, L"/");
						curr_os_path=ExtractFilePath(curr_os_path, L"/");

						if(!has_orig_path)
						{
							curr_orig_path = ExtractFilePath(curr_orig_path, orig_sep);
						}
					}
				}
				else //is file
				{
					std::wstring local_curr_os_path=convertToOSPathFromFileClient(curr_os_path+L"/"+osspecific_name);
					std::wstring srcpath=last_backuppath+local_curr_os_path;

					if(!has_orig_path)
					{
						metadata.orig_path = curr_orig_path + orig_sep + Server->ConvertToUTF8(cf.name);
					}

					bool copy_curr_file_entry=false;
					bool curr_has_hash = false;
					bool readd_curr_file_entry_sparse=false;
					std::string curr_sha2;
					{
						std::map<std::wstring, std::wstring>::iterator hash_it = 
							( (local_hash.get()==NULL)?extra_params.end():extra_params.find(L"sha512") );					
						if(hash_it!=extra_params.end())
						{
							curr_sha2 = base64_decode_dash(wnarrow(hash_it->second));
						}
					}

                    bool download_metadata=false;

					str_map::iterator sym_target = extra_params.find(L"sym_target");
					if(sym_target!=extra_params.end())
					{
						std::wstring symlink_path = backuppath+local_curr_os_path;
						if(!createSymlink(symlink_path, depth, sym_target->second, Server->ConvertToUnicode(orig_sep), true))
						{
							ServerLogger::Log(logid, L"Creating symlink at \""+symlink_path+L"\" to \""+sym_target->second+L" failed. " + widen(systemErrorInfo()), LL_ERROR);
							c_has_error=true;
							break;
						}
                        else
                        {
                            download_metadata=true;
                        }
					}
					else if(extra_params.find(L"special")!=extra_params.end())
					{
						std::wstring touch_path = backuppath+local_curr_os_path;
						std::auto_ptr<IFile> touch_file(Server->openFile(os_file_prefix(touch_path), MODE_WRITE));
						if(touch_file.get()==NULL)
						{
							ServerLogger::Log(logid, L"Error touching file at \""+touch_path+L"\". " + widen(systemErrorInfo()), LL_ERROR);
							c_has_error=true;
							break;
						}
						else
						{
							download_metadata=true;
						}
					}
					else if(indirchange || hasChange(line, diffs)) //is changed
					{
						bool f_ok=false;
						if(!curr_sha2.empty())
						{
							if(link_file(cf.name, osspecific_name, curr_path, curr_os_path, curr_sha2 , cf.size, true,
								metadata))
							{
								f_ok=true;
								linked_bytes+=cf.size;
                                download_metadata=true;
							}
						}

						if(!f_ok)
						{
							if(!r_offline || hasChange(line, modified_inplace_ids))
							{
								if(intra_file_diffs)
								{
									server_download->addToQueueChunked(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir);
								}
								else
								{
									server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir, false);
								}
							}
							else
							{
								download_nok_ids.add(line);
							}
						}
					}
					else if(!use_snapshots) //is not changed
					{						
						bool too_many_hardlinks;
						bool b=os_create_hardlink(os_file_prefix(backuppath+local_curr_os_path), os_file_prefix(srcpath), use_snapshots, &too_many_hardlinks);
						bool f_ok = false;
						bool copied_hashes = false;
						if(b)
						{
							f_ok=true;
						}
						else if(!b && too_many_hardlinks)
						{
							ServerLogger::Log(logid, L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Hardlink limit was reached. Copying file...", LL_DEBUG);
							copyFile(srcpath, backuppath+local_curr_os_path,
								with_hashes?(last_backuppath_hashes+local_curr_os_path):std::wstring(),
								with_hashes?(backuppath_hashes+local_curr_os_path):std::wstring(),
								metadata);
							f_ok=true;
							copied_hashes=true;
						}

						if(!f_ok) //creating hard link failed and not because of too many hard links per inode
						{
							if(link_logcnt<5)
							{
								ServerLogger::Log(logid, L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Loading file...", LL_WARNING);
							}
							else if(link_logcnt==5)
							{
								ServerLogger::Log(logid, L"More warnings of kind: Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Loading file... Skipping.", LL_WARNING);
							}
							else
							{
								Server->Log(L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Loading file...", LL_WARNING);
							}
							++link_logcnt;

							if(!curr_sha2.empty())
							{
								if(link_file(cf.name, osspecific_name, curr_path, curr_os_path, curr_sha2, cf.size, false,
									metadata))
								{
									f_ok=true;
									copy_curr_file_entry=copy_last_file_entries;						
									readd_curr_file_entry_sparse = readd_file_entries_sparse;
									linked_bytes+=cf.size;
                                    download_metadata=true;
								}
							}

							if(!f_ok)
							{
								if(intra_file_diffs)
								{
									server_download->addToQueueChunked(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir);
								}
								else
								{
									server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir, false);
								}
							}
						}
						else //created hard link successfully
						{
							copy_curr_file_entry=copy_last_file_entries;						
							readd_curr_file_entry_sparse = readd_file_entries_sparse;

							if(with_hashes && !copied_hashes)
							{
								curr_has_hash = os_create_hardlink(os_file_prefix(backuppath_hashes+local_curr_os_path), os_file_prefix(last_backuppath_hashes+local_curr_os_path), use_snapshots, NULL);
							}
						}
					}
					else
					{
						copy_curr_file_entry=copy_last_file_entries;
						readd_curr_file_entry_sparse = readd_file_entries_sparse;
						curr_has_hash = with_hashes;
					}

					if(copy_curr_file_entry)
					{
						ServerBackupDao::SFileEntry fileEntry = backup_dao->getFileEntryFromTemporaryTable(srcpath);

						if(fileEntry.exists)
						{
							addFileEntrySQLWithExisting(backuppath+local_curr_os_path, curr_has_hash?(backuppath_hashes+local_curr_os_path):std::wstring(),
								fileEntry.shahash, fileEntry.filesize, fileEntry.rsize, incremental_num);
							++num_copied_file_entries;

							readd_curr_file_entry_sparse=false;
						}
					}

					if(readd_curr_file_entry_sparse)
					{
						addSparseFileEntry(curr_path, cf, copy_file_entries_sparse_modulo, incremental_num,
							trust_client_hashes, curr_sha2, local_curr_os_path, curr_has_hash, server_hash_existing,
							num_readded_entries);
					}

                    if(download_metadata && client_main->getProtocolVersions().file_meta>0)
                    {
                        server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?0:-1,
                            metadata, script_dir, true);
                    }
				}
				++line;
			}
		}

		if(c_has_error)
			break;

		if(read<4096)
			break;
	}

	server_download->queueStop(false);
	if(server_hash_existing.get())
	{
		server_hash_existing->queueStop(false);
	}

	ServerLogger::Log(logid, L"Waiting for file transfers...", LL_INFO);

	while(!Server->getThreadPool()->waitFor(server_download_ticket, 1000))
	{
		if(files_size==0)
		{
			ServerStatus::setProcessPcDone(clientname, status_id, 100);
		}
		else
		{
			ServerStatus::setProcessPcDone(clientname, status_id,
				(std::min)(100,(int)(((float)(fc.getReceivedDataBytes() + (fc_chunked.get()?fc_chunked->getReceivedDataBytes():0) + linked_bytes))/((float)files_size/100.f)+0.5f)));
		}

		ServerStatus::setProcessQueuesize(clientname, status_id,
			(_u32)hashpipe->getNumElements(), (_u32)hashpipe_prepare->getNumElements());

		int64 ctime = Server->getTimeMS();
		if(ctime-last_eta_update>eta_update_intervall)
		{
			calculateEtaFileBackup(last_eta_update, eta_set_time, ctime, fc, fc_chunked.get(), linked_bytes, last_eta_received_bytes, eta_estimated_speed, files_size);
		}
	}

	if(server_download->isOffline() && !r_offline)
	{
		ServerLogger::Log(logid, L"Client "+clientname+L" went offline.", LL_ERROR);
		r_offline=true;
	}

	sendBackupOkay(!r_offline && !c_has_error);

	ServerLogger::Log(logid, L"Writing new file list...", LL_INFO);

	download_nok_ids.finalize();

	
	tmp->Seek(0);
	line = 0;
	list_parser.reset();
	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			bool b=list_parser.nextEntry(buffer[i], cf, NULL);
			if(b)
			{
				if(cf.isdir)
				{
					writeFileItem(clientlist, cf);
				}
				else if(server_download->isDownloadOk(line)
					 && !download_nok_ids.hasId(line))
				{
					if(server_download->isDownloadPartial(line))
					{
						cf.last_modified *= Server->getRandomNumber();
					}
					writeFileItem(clientlist, cf);
				}
				++line;
			}
		}
	}

	Server->destroy(clientlist);

	if(server_hash_existing_ticket!=ILLEGAL_THREADPOOL_TICKET)
	{
		ServerLogger::Log(logid, L"Waiting for file entry hashing thread...", LL_INFO);

		Server->getThreadPool()->waitFor(server_hash_existing_ticket);
	}

	addExistingHashesToDb(incremental_num);

	if(copy_last_file_entries || readd_file_entries_sparse)
	{
		if(num_readded_entries>0)
		{
			ServerLogger::Log(logid, L"Number of readded file entries is "+convert(num_readded_entries), LL_INFO);
		}

		if(num_copied_file_entries>0)
		{
			ServerLogger::Log(logid, L"Number of copyied file entries from last backup is "+convert(num_copied_file_entries), LL_INFO);
		}

		if(copy_last_file_entries)
		{
			backup_dao->dropTemporaryLastFilesTableIndex();
			backup_dao->dropTemporaryLastFilesTable();
		}
	}

	ServerLogger::Log(logid, L"Waiting for file hashing and copying threads...", LL_INFO);

	waitForFileThreads();

	if( bsh->hasError() || bsh_prepare->hasError() )
	{
		disk_error=true;
	}

	if(!r_offline && !c_has_error && !disk_error)
	{
		if(server_settings->getSettings()->end_to_end_file_backup_verification
			|| (client_main->isOnInternetConnection()
			&& server_settings->getSettings()->verify_using_client_hashes 
			&& server_settings->getSettings()->internet_calculate_filehashes_on_client) )
		{
			if(!verify_file_backup(tmp))
			{
				ServerLogger::Log(logid, "Backup verification failed", LL_ERROR);
				c_has_error=true;
			}
			else
			{
				ServerLogger::Log(logid, "Backup verification ok", LL_INFO);
			}
		}

		bool b=false;
		if(!c_has_error)
		{
			std::wstring dst_file=widen(clientlistName(group, true));

			db->BeginWriteTransaction();
			b=os_rename_file(dst_file, widen(clientlistName(group)));
			if(b)
			{
				backup_dao->setFileBackupDone(backupid);
			}
			db->EndTransaction();
		}

		if(b && (group==c_group_default || group==c_group_continuous) )
		{
			std::wstring name = L"current";
			if(group==c_group_continuous)
			{
				name = L"continuous";
			}

			ServerLogger::Log(logid, "Creating symbolic links. -1", LL_DEBUG);

			std::wstring backupfolder=server_settings->getSettings()->backupfolder;
			std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+name;

			os_remove_symlink_dir(os_file_prefix(currdir));		
			os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));
		}

		if(b && group==c_group_default)
		{
			ServerLogger::Log(logid, "Creating symbolic links. -2", LL_DEBUG);

			std::wstring backupfolder=server_settings->getSettings()->backupfolder;
			std::wstring currdir=backupfolder+os_file_sep()+L"clients";
			if(!os_create_dir(os_file_prefix(currdir)) && !os_directory_exists(os_file_prefix(currdir)))
			{
				ServerLogger::Log(logid, "Error creating \"clients\" dir for symbolic links", LL_ERROR);
			}
			currdir+=os_file_sep()+clientname;
			os_remove_symlink_dir(os_file_prefix(currdir));
			os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

			ServerLogger::Log(logid, "Symbolic links created.", LL_DEBUG);

			if(server_settings->getSettings()->create_linked_user_views)
			{
				ServerLogger::Log(logid, "Creating user views...", LL_INFO);

				createUserViews(tmp);
			}

			saveUsersOnClient();
		}
		else if(!b && !c_has_error)
		{
			ServerLogger::Log(logid, "Fatal error renaming clientlist.", LL_ERROR);
			ClientMain::sendMailToAdmins("Fatal error occured during incremental file backup", ServerLogger::getWarningLevelTextLogdata(logid));
		}
	}
	else if(!c_has_error && !disk_error)
	{
		ServerLogger::Log(logid, "Client disconnected while backing up. Copying partial file...", LL_DEBUG);
		db->BeginWriteTransaction();
		moveFile(widen(clientlistName(group, true)), widen(clientlistName(group, false)));
		backup_dao->setFileBackupDone(backupid);
		db->EndTransaction();
	}
	else
	{
		ServerLogger::Log(logid, "Fatal error during backup. Backup not completed", LL_ERROR);
		ClientMain::sendMailToAdmins("Fatal error occured during incremental file backup", ServerLogger::getWarningLevelTextLogdata(logid));
	}

	running_updater->stop();
	backup_dao->updateFileBackupRunning(backupid);
	Server->destroy(tmp);
	Server->deleteFile(tmpfilename);

	if(incr_backup_stoptime==0)
	{
		incr_backup_stoptime=Server->getTimeMS();
	}

	_i64 transferred_bytes=fc.getTransferredBytes()+(fc_chunked.get()?fc_chunked->getTransferredBytes():0);
	_i64 transferred_compressed=fc.getRealTransferredBytes()+(fc_chunked.get()?fc_chunked->getRealTransferredBytes():0);
	int64 passed_time=incr_backup_stoptime-incr_backup_starttime;
	ServerLogger::Log(logid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );
	if(transferred_compressed>0)
	{
		ServerLogger::Log(logid, "(Before compression: "+PrettyPrintBytes(transferred_compressed)+" ratio: "+nconvert((float)transferred_compressed/transferred_bytes)+")");
	}

	stopFileMetadataDownloadThread();

	if(group==c_group_default)
	{
		ClientMain::run_script(L"urbackup" + os_file_sep() + L"post_incr_filebackup", L"\""+ backuppath + L"\"", logid);
	}

	if(c_has_error) return false;

	return !r_offline;
}



SBackup IncrFileBackup::getLastIncremental( int group )
{
	ServerBackupDao::SLastIncremental last_incremental = backup_dao->getLastIncrementalFileBackup(clientid, group);
	if(last_incremental.exists)
	{
		SBackup b;
		b.incremental=last_incremental.incremental;
		b.path=last_incremental.path;
		b.is_complete=last_incremental.complete>0;
		b.is_resumed=last_incremental.resumed>0;
		b.backupid=last_incremental.id;


		ServerBackupDao::SLastIncremental last_complete_incremental =
			backup_dao->getLastIncrementalCompleteFileBackup(clientid, group);

		if(last_complete_incremental.exists)
		{
			b.complete=last_complete_incremental.path;
		}

		std::vector<ServerBackupDao::SDuration> durations = 
			backup_dao->getLastIncrementalDurations(clientid);

		ServerBackupDao::SDuration duration = interpolateDurations(durations);

		b.indexing_time_ms = duration.indexing_time_ms;
		b.backup_time_ms = duration.duration*1000;

		b.incremental_ref=0;
		return b;
	}
	else
	{
		SBackup b;
		b.incremental=-2;
		b.incremental_ref=0;
		return b;
	}
}

bool IncrFileBackup::deleteFilesInSnapshot(const std::string clientlist_fn, const std::vector<size_t> &deleted_ids, std::wstring snapshot_path, bool no_error)
{
	if(os_directory_exists(os_file_prefix(backuppath + os_file_sep() + L"user_views")))
	{
		os_remove_nonempty_dir(os_file_prefix(backuppath + os_file_sep() + L"user_views"));
	}

	FileListParser list_parser;

	IFile *tmp=Server->openFile(clientlist_fn, MODE_READ);
	if(tmp==NULL)
	{
		ServerLogger::Log(logid, "Could not open clientlist in ::deleteFilesInSnapshot", LL_ERROR);
		return false;
	}

	char buffer[4096];
	size_t read;
	SFile curr_file;
	size_t line=0;
	std::wstring curr_path=snapshot_path;
	std::wstring curr_os_path=snapshot_path;
	bool curr_dir_exists=true;

	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			if(list_parser.nextEntry(buffer[i], curr_file, NULL))
			{
				if(curr_file.isdir)
				{
					if(curr_file.name==L"..")
					{
						curr_path=ExtractFilePath(curr_path, L"/");
						curr_os_path=ExtractFilePath(curr_os_path, L"/");
						if(!curr_dir_exists)
						{
							curr_dir_exists=os_directory_exists(curr_path);
						}
					}
				}

				if( hasChange(line, deleted_ids) )
				{
					std::wstring osspecific_name=fixFilenameForOS(curr_file.name);
					std::wstring curr_fn=convertToOSPathFromFileClient(curr_os_path+os_file_sep()+osspecific_name);
					if(curr_file.isdir)
					{
						if(curr_dir_exists)
						{
							if(!remove_directory_link_dir(curr_fn, *backup_dao, clientid) )
							{
								if(!no_error)
								{
									ServerLogger::Log(logid, L"Could not remove directory \""+curr_fn+L"\" in ::deleteFilesInSnapshot - " + widen(systemErrorInfo()), LL_ERROR);
									Server->destroy(tmp);
									return false;
								}
							}
						}
						curr_path+=os_file_sep()+curr_file.name;
						curr_os_path+=os_file_sep()+osspecific_name;
						curr_dir_exists=false;
					}
					else
					{
						if( curr_dir_exists )
						{
							ServerLogger::Log(logid, L"Removing file \""+curr_fn+L"\" in ::deleteFilesInSnapshot", LL_DEBUG);
							if( !Server->deleteFile(os_file_prefix(curr_fn)) )
							{
								if(!no_error)
								{
									std::auto_ptr<IFile> tf(Server->openFile(os_file_prefix(curr_fn), MODE_READ));
									if(tf.get()!=NULL)
									{
										ServerLogger::Log(logid, L"Could not remove file \""+curr_fn+L"\" in ::deleteFilesInSnapshot - " + widen(systemErrorInfo()), LL_ERROR);
									}
									else
									{
										ServerLogger::Log(logid, L"Could not remove file \""+curr_fn+L"\" in ::deleteFilesInSnapshot - " + widen(systemErrorInfo())+L". It was already deleted.", LL_ERROR);
									}
									Server->destroy(tmp);
									return false;
								}
							}
						}
					}
				}
				else if( curr_file.isdir && curr_file.name!=L".." )
				{
					curr_path+=os_file_sep()+curr_file.name;
					curr_os_path+=os_file_sep()+fixFilenameForOS(curr_file.name);
				}
				++line;
			}
		}
	}

	Server->destroy(tmp);
	return true;
}

void IncrFileBackup::addExistingHash( const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize , int64 rsize)
{
	ServerBackupDao::SFileEntry file_entry;
	file_entry.exists = true;
	file_entry.fullpath = fullpath;
	file_entry.hashpath = hashpath;
	file_entry.shahash = shahash;
	file_entry.filesize = filesize;
	file_entry.rsize = rsize;

	IScopedLock lock(hash_existing_mutex);
	hash_existing.push_back(file_entry);
}

void IncrFileBackup::addExistingHashesToDb(int incremental)
{
	IScopedLock lock(hash_existing_mutex);
	for(size_t i=0;i<hash_existing.size();++i)
	{
		addFileEntrySQLWithExisting(hash_existing[i].fullpath, hash_existing[i].hashpath,
			hash_existing[i].shahash, hash_existing[i].filesize, hash_existing[i].rsize, incremental);
	}
	hash_existing.clear();
}

void IncrFileBackup::addFileEntrySQLWithExisting( const std::wstring &fp, const std::wstring &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int incremental)
{
	int64 entryid = fileindex->get_with_cache_exact(FileIndex::SIndexKey(shahash.c_str(), filesize, clientid));

	if(entryid==0)
	{
		Server->Log(L"File entry with filesize "+convert(filesize)+L" to file with path \""+fp+L"\" should exist but does not.", LL_WARNING);
		return;
	}

	ServerBackupDao::SFindFileEntry fentry = backup_dao->getFileEntry(entryid);
	if(!fentry.exists)
	{
		Server->Log(L"File entry in database with id " +convert(entryid)+L" and filesize "+convert(filesize)+L" to file with path \""+fp+L"\" should exist but does not.", LL_WARNING);
		return;
	}

	if(rsize<0)
	{
		rsize=fentry.rsize;
	}

	BackupServerHash::addFileSQL(*backup_dao, *fileindex.get(), backupid, clientid, incremental, fp, hash_path,
		shahash, filesize, rsize, entryid, clientid, fentry.next_entry, false);
}

void IncrFileBackup::addSparseFileEntry( std::wstring curr_path, SFile &cf, int copy_file_entries_sparse_modulo, int incremental_num, bool trust_client_hashes, std::string &curr_sha2,
	std::wstring local_curr_os_path, bool curr_has_hash, std::auto_ptr<ServerHashExisting> &server_hash_existing, size_t& num_readded_entries )
{
	if(cf.size<c_readd_size_limit)
	{
		return;
	}

	std::string curr_file_path = Server->ConvertToUTF8(curr_path + L"/" + cf.name);
	int crc32 = static_cast<int>(urb_adler32(0, curr_file_path.c_str(), static_cast<unsigned int>(curr_file_path.size())));
	if(crc32 % copy_file_entries_sparse_modulo == incremental_num )
	{
		if(trust_client_hashes && !curr_sha2.empty())
		{
			addFileEntrySQLWithExisting(backuppath+local_curr_os_path, curr_has_hash?(backuppath_hashes+local_curr_os_path):std::wstring(), 
				curr_sha2, cf.size, -1, incremental_num);

			++num_readded_entries;
		}							
		else if(server_hash_existing.get())
		{
			addExistingHashesToDb(incremental_num);
			server_hash_existing->queueFile(backuppath+local_curr_os_path, curr_has_hash?(backuppath_hashes+local_curr_os_path):std::wstring());
			++num_readded_entries;
		}
	}
}

void IncrFileBackup::copyFile(const std::wstring& source, const std::wstring& dest,
	const std::wstring& hash_src, const std::wstring& hash_dest,
	const FileMetadata& metadata)
{
	CWData data;
	data.addInt(BackupServerHash::EAction_Copy);
	data.addString(Server->ConvertToUTF8(source));
	data.addString(Server->ConvertToUTF8(dest));
	data.addString(Server->ConvertToUTF8(hash_src));
	data.addString(Server->ConvertToUTF8(hash_dest));
	metadata.serialize(data);

	hashpipe->Write(data.getDataPtr(), data.getDataSize());
}

bool IncrFileBackup::doFullBackup()
{
	client_main->stopBackupRunning(true);
	active_thread->Exit();

	ServerStatus::stopProcess(clientname, status_id);

	FullFileBackup full_backup(client_main, clientid, clientname, LogAction_NoLogging, group, use_tmpfiles, tmpfile_path, use_reflink, use_snapshots);
	full_backup();

	disk_error = full_backup.hasDiskError();
	has_early_error = full_backup.hasEarlyError();
	backupid = full_backup.getBackupid();

	log_action = LogAction_NoLogging;

	client_main->startBackupRunning(true);

	return full_backup.getResult();
}
