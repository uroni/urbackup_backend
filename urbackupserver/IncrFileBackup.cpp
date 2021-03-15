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
#include "IncrFileBackup.h"
#include "server_log.h"
#include "snapshot_helper.h"
#include "../urbackupcommon/os_functions.h"
#include "ClientMain.h"
#include "treediff/TreeDiff.h"
#include "../urbackupcommon/filelist_utils.h"
#include "server_dir_links.h"
#include "server_running.h"
#include "server_cleanup.h"
#include "ServerDownloadThreadGroup.h"
#include "FileIndex.h"
#include <stack>
#include "../urbackupcommon/file_metadata.h"
#include "server.h"
#include "../common/adler32.h"
#include "../common/data.h"
#include "FullFileBackup.h"
#include "FileMetadataDownloadThread.h"
#include "database.h"
#include <algorithm>
#include "PhashLoad.h"

extern std::string server_identity;

const int64 c_readd_size_limit=4096;

namespace
{
	
}

IncrFileBackup::IncrFileBackup( ClientMain* client_main, int clientid, std::string clientname, std::string clientsubname, LogAction log_action,
	int group, bool use_tmpfiles, std::string tmpfile_path, bool use_reflink, bool use_snapshots, std::string server_token, std::string details, bool scheduled)
	: FileBackup(client_main, clientid, clientname, clientsubname, log_action, true, group, use_tmpfiles, tmpfile_path, use_reflink, use_snapshots, server_token, details, scheduled), 
	hash_existing_mutex(NULL), filesdao(NULL), link_dao(NULL), link_journal_dao(NULL)
{

}

bool IncrFileBackup::doFileBackup()
{
	ScopedFreeObjRef<ServerFilesDao*> free_filesdao(filesdao);
	filesdao = new ServerFilesDao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES));
	ScopedFreeObjRef<ServerLinkDao*> free_link_dao(link_dao);
	ScopedFreeObjRef<ServerLinkJournalDao*> free_link_journal_dao(link_journal_dao);

	ServerLogger::Log(logid, std::string("Starting ") + (scheduled ? "scheduled" : "unscheduled") + " incremental file backup...", LL_INFO);

	if(with_hashes)
	{
		ServerLogger::Log(logid, clientname+": Doing backup with hashes...", LL_DEBUG);
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
		ServerLogger::Log(logid, clientname+": Doing backup with intra file diffs...", LL_DEBUG);
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
			ServerStatus::changeProcess(clientname, status_id, sa_resume_full_file);
		}
		else
		{
			ServerStatus::changeProcess(clientname, status_id, sa_resume_incr_file);
		}
	}

	bool no_backup_dirs=false;
	bool connect_fail = false;
	bool b=request_filelist_construct(resumed_full, resumed_backup, group, true, no_backup_dirs, connect_fail, clientsubname);
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
		ServerLogger::Log(logid, clientname+": Doing backup with hashed transfer...", LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(logid, clientname+": Doing backup without hashed transfer...", LL_DEBUG);
	}

	Server->Log(clientname+": Connecting to client...", LL_DEBUG);
	std::string identity = client_main->getIdentity();
	FileClient fc(false, identity, client_main->getProtocolVersions().filesrv_protocol_version, client_main->isOnInternetConnection(), client_main, use_tmpfiles?NULL:this);
	std::auto_ptr<FileClientChunked> fc_chunked;
	if(intra_file_diffs)
	{
		if(client_main->getClientChunkedFilesrvConnection(fc_chunked, server_settings.get(), this, 60000))
		{
			fc_chunked->setProgressLogCallback(this);
			fc_chunked->setDestroyPipe(true);
			if(fc_chunked->hasError())
			{
				ServerLogger::Log(logid, "Incremental Backup of "+clientname+" failed - CONNECT error -1", LL_ERROR);
				has_early_error=true;
				log_backup=false;
				return false;
			}
		}
		else
		{
			ServerLogger::Log(logid, "Incremental Backup of "+clientname+" failed - CONNECT error -3", LL_ERROR);
			has_early_error=true;
			log_backup=false;
			return false;
		}
	}
	_u32 rc=client_main->getClientFilesrvConnection(&fc, server_settings.get(), 60000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(logid, "Incremental Backup of "+clientname+" failed - CONNECT error -2", LL_ERROR);
		has_early_error=true;
		log_backup=false;
		return false;
	}

	fc.setProgressLogCallback(this);

	ServerLogger::Log(logid, clientname+": Loading file list...", LL_INFO);
	IFsFile* tmp_filelist = ClientMain::getTemporaryFileRetry(use_tmpfiles, tmpfile_path, logid);
	ScopedDeleteFile tmp_filelist_delete(tmp_filelist);
	if(tmp_filelist==NULL)
	{
		ServerLogger::Log(logid, "Error creating temporary file in ::doIncrBackup", LL_ERROR);
		return false;
	}

	int64 incr_backup_starttime=Server->getTimeMS();
	int64 incr_backup_stoptime=0;

	rc=fc.GetFile(group>0?("urbackup/filelist_"+convert(group)+".ub"):"urbackup/filelist.ub", tmp_filelist, hashed_transfer, false, 0, false, 0);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(logid, "Error getting filelist of "+clientname+". Errorcode: "+fc.getErrorString(rc)+" ("+convert(rc)+")", LL_ERROR);
		has_early_error=true;
		return false;
	}

	ServerLogger::Log(logid, clientname+" Starting incremental backup...", LL_DEBUG);

	int incremental_num = resumed_full?0:(last.incremental+1);
	if (!backup_dao->newFileBackup(incremental_num, clientid, backuppath_single, resumed_backup, Server->getTimeMS() - indexing_start_time, group))
	{
		ServerLogger::Log(logid, "Error creating new backup row in database", LL_ERROR);
		has_early_error = true;
		return false;
	}
	backupid=static_cast<int>(db->getLastInsertID());

	std::string backupfolder=server_settings->getSettings()->backupfolder;
	std::string last_backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path;
	std::string last_backuppath_hashes=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path+os_file_sep()+".hashes";
	std::string last_backuppath_complete=backupfolder+os_file_sep()+clientname+os_file_sep()+last.complete;

	std::string tmpfilename=tmp_filelist->getFilename();
	tmp_filelist_delete.release();
	Server->destroy(tmp_filelist);

	ServerLogger::Log(logid, clientname+": Calculating file tree differences...", LL_INFO);

	bool reflink_files = !intra_file_diffs;

	bool error=false;
	std::vector<size_t> deleted_ids;
	std::vector<size_t> *deleted_ids_ref=NULL;
	if(use_snapshots) deleted_ids_ref=&deleted_ids;
	std::vector<size_t> large_unchanged_subtrees;
	std::vector<size_t> *large_unchanged_subtrees_ref=NULL;
	if(use_directory_links) large_unchanged_subtrees_ref=&large_unchanged_subtrees;
	std::vector<size_t> modified_inplace_ids;
	std::vector<size_t> deleted_inplace_ids;
	std::vector<size_t>* deleted_inplace_ids_ref = NULL;
	if (!reflink_files) deleted_inplace_ids_ref = &deleted_inplace_ids;
	std::vector<size_t> dir_diffs;

	std::string clientlist_name = clientlistName(last.backupid);
	if(!Server->fileExists(clientlist_name))
	{
		clientlist_name="urbackup/clientlist_"+convert(clientid)+".ub";
	}

	std::vector<size_t> diffs;
	{
		bool has_symbit = client_main->getProtocolVersions().symbit_version > 0;
		std::string os_simple = client_main->getProtocolVersions().os_simple;
		bool is_windows = (os_simple == "windows" || os_simple.empty());
		diffs = TreeDiff::diffTrees(clientlist_name, tmpfilename,
			error, deleted_ids_ref, large_unchanged_subtrees_ref, &modified_inplace_ids,
			dir_diffs, deleted_inplace_ids_ref, has_symbit, is_windows);
	}

	if(error)
	{
		if(!client_main->isOnInternetConnection())
		{
			ServerLogger::Log(logid, "Error while calculating tree diff. Doing full backup.", LL_ERROR);
			deleteBackup();
			return doFullBackup();
		}
		else
		{
			ServerLogger::Log(logid, "Error while calculating tree diff. Not doing full backup because of internet connection.", LL_ERROR);
			has_early_error=true;
			return false;
		}
	}


	bool make_subvolume_readonly = false;
	bool crossvolume_links = false;
	if(use_snapshots)
	{
		make_subvolume_readonly = true;
		crossvolume_links = true;

		bool zfs_file = BackupServer::getSnapshotMethod(false) == BackupServer::ESnapshotMethod_ZfsFile;
		if (zfs_file)
		{
			if (os_get_file_type(backuppath) != 0)
			{
				ServerLogger::Log(logid, "File/Directory at " + backuppath + " already exists.", LL_ERROR);
				has_early_error = true;
				allow_remove_backup_folder = false;
				return false;
			}

			std::auto_ptr<IFile> touch_f(Server->openFile(backuppath, MODE_WRITE));
			if (touch_f.get() == NULL)
			{
				ServerLogger::Log(logid, "Could not touch file " + backuppath + ". " + os_last_error_str(), LL_ERROR);
				has_early_error = true;
				return false;
			}
			touch_f->Sync();
		}

		ServerLogger::Log(logid, clientname+": Creating snapshot...", LL_INFO);
		std::string errmsg;
		if(!SnapshotHelper::snapshotFileSystem(false, clientname, last.path, backuppath_single+ ".startup-del", errmsg)
			|| !SnapshotHelper::isSubvolume(false, clientname, backuppath_single+ ".startup-del") )
		{
			errmsg = trim(errmsg);
			ServerLogger::Log(logid, "Creating new snapshot failed (Server error) "
				+(errmsg.empty()?os_last_error_str(): ("\""+errmsg+"\"")), LL_WARNING);

			if (SnapshotHelper::isSubvolume(false, clientname, backuppath_single+".startup-del")
				|| SnapshotHelper::isSubvolume(false, clientname, backuppath_single) )
			{
				if (zfs_file)
				{
					Server->deleteFile(backuppath);
				}

				ServerLogger::Log(logid, "Subvolume already exists.", LL_ERROR);
				has_early_error = true;
				allow_remove_backup_folder = false;
				return false;
			}

			errmsg.clear();
			if(!SnapshotHelper::createEmptyFilesystem(false, clientname, backuppath_single, errmsg) )
			{
				if (zfs_file)
				{
					Server->deleteFile(backuppath);
				}

				ServerLogger::Log(logid, "Creating empty filesystem failed (Server error) "
					+ (errmsg.empty() ? os_last_error_str() : ("\"" + errmsg + "\"")), LL_ERROR);
				has_early_error=true;
				return false;
			}
			else if (zfs_file)
			{
				std::string mountpoint = SnapshotHelper::getMountpoint(false, clientname, backuppath_single);
				if (mountpoint.empty())
				{
					ServerLogger::Log(logid, "Could not find mountpoint of snapshot of client " + clientname + " path " + backuppath_single, LL_ERROR);
					has_early_error = true;
					return false;
				}

				if (!os_link_symbolic(mountpoint, backuppath + ".startup-del"))
				{
					ServerLogger::Log(logid, "Could create symlink to mountpoint at " + backuppath + " to " + mountpoint + ". " + os_last_error_str(), LL_ERROR);
					has_early_error = true;
					return false;
				}

				if (!os_rename_file(backuppath + ".startup-del", backuppath))
				{
					ServerLogger::Log(logid, "Could rename symlink at " + backuppath + ".startup-del to " + backuppath + ". " + os_last_error_str(), LL_ERROR);
					has_early_error = true;
					return false;
				}
			}

			if(!os_create_dir(os_file_prefix(backuppath_hashes)))
			{
				ServerLogger::Log(logid, "Cannot create hash path (Server error)", LL_ERROR);
				has_early_error=true;
				return false;
			}

			use_snapshots=false;
		}
		else
		{
			if (zfs_file)
			{
				std::string mountpoint = SnapshotHelper::getMountpoint(false, clientname, backuppath_single);
				if (mountpoint.empty())
				{
					ServerLogger::Log(logid, "Could not find mountpoint of snapshot of client " + clientname + " path " + backuppath_single, LL_ERROR);
					has_early_error = true;
					return false;
				}

				if (!os_link_symbolic(mountpoint, backuppath + ".startup-del"))
				{
					ServerLogger::Log(logid, "Could create symlink to mountpoint at " + backuppath + " to " + mountpoint + ". " + os_last_error_str(), LL_ERROR);
					has_early_error = true;
					return false;
				}

				Server->deleteFile(os_file_prefix(backuppath + ".startup-del" + os_file_sep() + ".hashes" + os_file_sep() + sync_fn));
				os_sync(backuppath + ".startup-del" + os_file_sep() + ".hashes");

				if (!os_rename_file(backuppath + ".startup-del", backuppath))
				{
					ServerLogger::Log(logid, "Could rename symlink at " + backuppath + ".startup-del to " + backuppath + ". " + os_last_error_str(), LL_ERROR);
					has_early_error = true;
					return false;
				}

				if (FileExists(backuppath_hashes + os_file_sep() + sync_fn))
				{
					ServerLogger::Log(logid, "Could not delete sync file. File still exists.", LL_ERROR);
					has_early_error = true;
					return false;
				}
			}
			else
			{
				Server->deleteFile(os_file_prefix(backuppath + ".startup-del" + os_file_sep() + ".hashes" + os_file_sep() + sync_fn));
				os_sync(backuppath + ".startup-del" + os_file_sep() + ".hashes");

				if (FileExists(backuppath_hashes + ".startup-del" + os_file_sep() + sync_fn))
				{
					ServerLogger::Log(logid, "Could not delete sync file. File still exists.", LL_ERROR);
					has_early_error = true;
					return false;
				}

				if (!os_rename_file(backuppath + ".startup-del",
					backuppath))
				{
					ServerLogger::Log(logid, "Error renaming new backup subvolume from " + backuppath + ".startup-del to " + backuppath + ". "
						+ os_last_error_str(), LL_ERROR);
					has_early_error = true;
					return false;
				}
			}
		}
	}
	else if (BackupServer::canReflink()
		&& !BackupServer::canHardlink())
	{
		crossvolume_links = true;
	}

	getTokenFile(fc, hashed_transfer, false);

	if(use_snapshots)
	{
		ServerLogger::Log(logid, clientname+": Deleting files in snapshot... ("+convert(deleted_ids.size() - deleted_inplace_ids.size())+")", LL_INFO);
		if(!deleteFilesInSnapshot(clientlist_name, deleted_ids, backuppath, false, false, deleted_inplace_ids_ref) )
		{
			ServerLogger::Log(logid, "Deleting files in snapshot failed (Server error)", LL_ERROR);
			has_early_error=true;
			return false;
		}

		ServerLogger::Log(logid, clientname+": Deleting files in hash snapshot...("+convert(deleted_ids.size())+")", LL_INFO);
		if(!deleteFilesInSnapshot(clientlist_name, deleted_ids, backuppath_hashes, true, true, NULL))
		{
			ServerLogger::Log(logid, "Deleting files in hash snapshot failed (Server error)", LL_ERROR);
			has_early_error=true;
			return false;
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

	int copy_file_entries_sparse_modulo = (std::max)(1,
		(std::min)(server_settings->getSettings()->min_file_incr,
			server_settings->getSettings()->max_file_incr) );

	if (copy_last_file_entries)
	{
		ServerLogger::Log(logid, clientname + ": Indexing file entries from last backup...", LL_INFO);
		copy_last_file_entries = copy_last_file_entries && filesdao->createTemporaryLastFilesTable();
		copy_last_file_entries = copy_last_file_entries && filesdao->copyToTemporaryLastFilesTable(last.backupid);
		filesdao->createTemporaryLastFilesTableIndex();
	}

	if(copy_last_file_entries && resumed_full)
	{
		readd_file_entries_sparse=false;
	}

	tmp_filelist = Server->openFile(tmpfilename, MODE_READ);
	tmp_filelist_delete.reset(tmp_filelist);

	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, false);
	Server->getThreadPool()->execute(running_updater, "backup active updater");

	bool with_sparse_hashing = client_main->getProtocolVersions().select_sha_version > 0;

	ServerLogger::Log(logid, clientname + ": Calculating tree difference size...", LL_INFO);
	bool backup_with_components;
	_i64 files_size = getIncrementalSize(tmp_filelist, diffs, backup_with_components);

	std::auto_ptr<ServerDownloadThreadGroup> server_download(new ServerDownloadThreadGroup(fc, fc_chunked.get(), backuppath,
		backuppath_hashes, last_backuppath, last_backuppath_complete,
		hashed_transfer, intra_file_diffs, clientid, clientname, clientsubname,
		use_tmpfiles, tmpfile_path, server_token, use_reflink,
		backupid, r_incremental, hashpipe_prepare, client_main, client_main->getProtocolVersions().filesrv_protocol_version,
		incremental_num, logid, with_hashes, shares_without_snapshot, with_sparse_hashing, metadata_download_thread.get(),
		backup_with_components, server_settings->getSettings()->download_threads, server_settings.get(), 
		intra_file_diffs, filepath_corrections, max_file_id));

	bool queue_downloads = client_main->getProtocolVersions().filesrv_protocol_version>2;

	char buffer[4096];
	_u32 read;
	std::string curr_path;
	std::string curr_os_path;
	std::string curr_hash_path;
	std::string curr_orig_path;
	std::string orig_sep;
	SFile cf;
	int depth=0;
	size_t line=0;
	int link_logcnt=0;
	bool indirchange=false;
	int changelevel;
	bool r_offline=false;
	_i64 filelist_size=tmp_filelist->Size();
	_i64 filelist_currpos=0;
	IdRange download_nok_ids;

	fc.resetReceivedDataBytes(true);
	if(fc_chunked.get()!=NULL)
	{
		fc_chunked->resetReceivedDataBytes(true);
	}

	ServerStatus::setProcessTotalBytes(clientname, status_id, files_size);

	tmp_filelist->Seek(0);

	int64 laststatsupdate=0;
	int64 last_eta_update=0;
	int64 last_eta_received_bytes=0;
	double eta_estimated_speed=0;

	int64 linked_bytes = 0;

	ServerLogger::Log(logid, clientname+": Linking unchanged and loading new files...", LL_INFO);

	FileListParser list_parser;

	bool c_has_error=false;
	bool backup_stopped=false;
	size_t skip_dir_completely=0;
	bool skip_dir_copy_sparse = false;
	bool script_dir=false;
	std::stack<std::set<std::string> > folder_files;
	folder_files.push(std::set<std::string>());
	std::vector<size_t> folder_items;
	folder_items.push_back(0);
	std::stack<bool> dir_diff_stack;
	std::stack<int64> dir_ids;
	std::map<int64, int64> dir_end_ids;
	bool phash_load_offline = false;

	bool has_read_error = false;
	while( (read=tmp_filelist->Read(buffer, 4096, &has_read_error))>0 )
	{
		if (has_read_error)
		{
			break;
		}

		filelist_currpos+=read;

		for(size_t i=0;i<read;++i)
		{
			std::map<std::string, std::string> extra_params;
			bool b=list_parser.nextEntry(buffer[i], cf, &extra_params);
			if(b)
			{
				std::string osspecific_name;

				if(!cf.isdir || cf.name!="..")
				{
					osspecific_name = fixFilenameForOS(cf.name, folder_files.top(), curr_path, true, logid, filepath_corrections);
				}

				if(skip_dir_completely>0)
				{
					if(cf.isdir)
					{						
						if(cf.name=="..")
						{
							--skip_dir_completely;
							if(skip_dir_completely>0)
							{
								curr_os_path=ExtractFilePath(curr_os_path, "/");
								curr_path=ExtractFilePath(curr_path, "/");
								folder_files.pop();
								dir_ids.pop();
							}
							else
							{
								max_file_id.setMaxPreProcessed(line);
							}
						}
						else
						{
							curr_os_path+="/"+osspecific_name;
							curr_path+="/"+cf.name;
							++skip_dir_completely;
							folder_files.push(std::set<std::string>());
							dir_ids.push(line);
						}
					}
					else if( skip_dir_copy_sparse
						&& extra_params.find("sym_target")==extra_params.end()
						&& extra_params.find("special")==extra_params.end() )
					{
						std::string local_curr_os_path=convertToOSPathFromFileClient(curr_os_path+"/"+osspecific_name);
						addSparseFileEntry(curr_path, cf, copy_file_entries_sparse_modulo, incremental_num,
							local_curr_os_path, num_readded_entries);
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
						if (!backup_stopped)
						{
							if (ServerStatus::getProcess(clientname, status_id).stop)
							{
								r_offline = true;
								backup_stopped = true;
								should_backoff = false;
								ServerLogger::Log(logid, "Server admin stopped backup.", LL_ERROR);
								server_download->queueSkip();
							}
						}

						laststatsupdate = ctime;
						if (files_size == 0)
						{
							ServerStatus::setProcessPcDone(clientname, status_id, 100);
						}
						else
						{
							int64 done_bytes = fc.getReceivedDataBytes(true)
								+ (fc_chunked.get() ? fc_chunked->getReceivedDataBytes(true) : 0) + linked_bytes;
							ServerStatus::setProcessDoneBytes(clientname, status_id, done_bytes);
							ServerStatus::setProcessPcDone(clientname, status_id,
								(std::min)(100, (int)(((float)done_bytes) / ((float)files_size / 100.f) + 0.5f)));
						}

						ServerStatus::setProcessQueuesize(clientname, status_id,
							(_u32)hashpipe->getNumElements(), (_u32)hashpipe_prepare->getNumElements());
					}

					if (ctime - last_eta_update > eta_update_intervall)
					{
						calculateEtaFileBackup(last_eta_update, eta_set_time, ctime, fc, fc_chunked.get(), linked_bytes, last_eta_received_bytes, eta_estimated_speed, files_size);
					}

					calculateDownloadSpeed(ctime, fc, fc_chunked.get());
				} while (server_download->sleepQueue());

				if(server_download->isOffline() && !r_offline)
				{
					ServerLogger::Log(logid, "Client "+clientname+" went offline.", LL_ERROR);
					r_offline=true;
					incr_backup_stoptime=Server->getTimeMS();
				}


				if(cf.isdir)
				{
					if(!indirchange && hasChange(line, diffs) )
					{
						indirchange=true;
						changelevel=depth;

						if(cf.name=="..")
						{
							--changelevel;
						}
					}

					if(cf.name!="..")
					{
						bool dir_diff = false;
						if(!indirchange)
						{
							dir_diff = hasChange(line, dir_diffs);
						}						

						dir_diff_stack.push(dir_diff);

						if(indirchange || dir_diff)
						{
							for(size_t j=0;j<folder_items.size();++j)
							{
								++folder_items[j];
							}
						}

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

						bool dir_linked=false;
						if(use_directory_links && hasChange(line, large_unchanged_subtrees) )
						{
							std::string srcpath=last_backuppath+local_curr_os_path;
							std::string src_hashpath = last_backuppath_hashes+local_curr_os_path;
							if(link_directory_pool(clientid, backuppath+local_curr_os_path, srcpath, dir_pool_path,
								 BackupServer::isFilesystemTransactionEnabled(), link_dao, link_journal_dao, depth) )
							{
								if(link_directory_pool(clientid, backuppath_hashes + local_curr_os_path, src_hashpath, dir_pool_path,
									BackupServer::isFilesystemTransactionEnabled(), link_dao, link_journal_dao, depth) )
								{
									skip_dir_completely = 1;
									dir_linked = true;

									if (copy_last_file_entries)
									{
										std::vector<ServerFilesDao::SFileEntry> file_entries = filesdao->getFileEntriesFromTemporaryTableGlob(escape_glob_sql(srcpath) + os_file_sep() + "*");
										for (size_t i = 0; i < file_entries.size(); ++i)
										{
											if (file_entries[i].fullpath.size() > srcpath.size())
											{
												std::string entry_hashpath;
												if (next(file_entries[i].hashpath, 0, src_hashpath))
												{
													entry_hashpath = backuppath_hashes + local_curr_os_path + file_entries[i].hashpath.substr(src_hashpath.size());
												}

												addFileEntrySQLWithExisting(backuppath + local_curr_os_path + file_entries[i].fullpath.substr(srcpath.size()), entry_hashpath,
													file_entries[i].shahash, file_entries[i].filesize, file_entries[i].filesize, incremental_num);

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
								else
								{
									std::auto_ptr<DBScopedSynchronous> link_dao_synchronous;
									if (!remove_directory_link(backuppath + local_curr_os_path, *link_dao, clientid, link_dao_synchronous))
									{
										ServerLogger::Log(logid, "Could not remove symlinked directory \"" + backuppath + local_curr_os_path + "\" after symlinking metadata directory failed.", LL_ERROR);
										c_has_error = true;
										break;
									}
								}
							}
						}
						if(!dir_linked && (!use_snapshots || indirchange || dir_diff) )
						{
							bool dir_already_exists = (use_snapshots && dir_diff);
							str_map::iterator sym_target = extra_params.find("sym_target");

							bool symlinked_file = false;

							std::string metadata_srcpath=last_backuppath_hashes+local_curr_os_path + os_file_sep()+metadata_dir_fn;

							if(sym_target!=extra_params.end())
							{
								if(dir_already_exists)
								{
									bool prev_is_symlink = (os_get_file_type(os_file_prefix(backuppath + local_curr_os_path)) & EFileType_Symlink)>0;

									if (prev_is_symlink)
									{
										if (!os_remove_symlink_dir(os_file_prefix(backuppath + local_curr_os_path)) )
										{
											ServerLogger::Log(logid, "Could not remove symbolic link at \"" + backuppath + local_curr_os_path + "\" " + systemErrorInfo(), LL_ERROR);
											c_has_error = true;
											break;
										}

										metadata_srcpath = last_backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + "/" + escape_metadata_fn(osspecific_name));
									}
									else
									{
										//Directory to directory symlink
										if (!os_remove_dir(os_file_prefix(backuppath + local_curr_os_path)) )
										{
											ServerLogger::Log(logid, "Could not remove directory at \"" + backuppath + local_curr_os_path + "\" " + systemErrorInfo(), LL_ERROR);
											c_has_error = true;
											break;
										}

										if ( !Server->deleteFile(os_file_prefix(metadata_fn)) )
										{
											ServerLogger::Log(logid, "Error deleting metadata file \"" + metadata_fn + "\". " + os_last_error_str(), LL_WARNING);
										}
									}
								}
								else
								{
									metadata_srcpath = last_backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + "/" + escape_metadata_fn(osspecific_name));
								}

								if(!createSymlink(backuppath+local_curr_os_path, depth, sym_target->second, orig_sep, true))
								{
									ServerLogger::Log(logid, "Creating symlink at \""+backuppath+local_curr_os_path+"\" to \""+sym_target->second+"\" failed. " + systemErrorInfo(), LL_ERROR);
									c_has_error=true;
									break;
								}					

								metadata_fn = backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + "/" + escape_metadata_fn(osspecific_name));
								
								symlinked_file=true;
							}
							else if( !dir_already_exists )
							{
								if(!os_create_dir(os_file_prefix(backuppath+local_curr_os_path)) )
								{
									std::string errstr = os_last_error_str();
									if(!os_directory_exists(os_file_prefix(backuppath+local_curr_os_path)))
									{
										ServerLogger::Log(logid, "Creating directory  \""+backuppath+local_curr_os_path+"\" failed. - " + errstr, LL_ERROR);
										c_has_error=true;
										break;
									}
									else
									{
										ServerLogger::Log(logid, "Directory \""+backuppath+local_curr_os_path+"\" does already exist.", LL_WARNING);
									}
								}								
							}
							
							if(!dir_already_exists && !symlinked_file)
							{
								if(!os_create_dir(os_file_prefix(backuppath_hashes+local_curr_os_path)))
								{
									std::string errstr = os_last_error_str();
									if(!os_directory_exists(os_file_prefix(backuppath_hashes+local_curr_os_path)))
									{
										ServerLogger::Log(logid, "Creating directory  \""+backuppath_hashes+local_curr_os_path+"\" failed. - " + errstr, LL_ERROR);
										c_has_error=true;
										break;
									}
									else
									{
										ServerLogger::Log(logid, "Directory  \""+backuppath_hashes+local_curr_os_path+"\" does already exist. - " + errstr, LL_WARNING);
									}
								}								
							}

							if(dir_already_exists)
							{
								if(!Server->deleteFile(os_file_prefix(metadata_fn)))
								{
									if (sym_target == extra_params.end() )
									{
										if(os_get_file_type(os_file_prefix(backuppath + local_curr_os_path)) & EFileType_Symlink )
										{
											//Directory symlink to directory
											if (!os_remove_symlink_dir(os_file_prefix(backuppath + local_curr_os_path)) )
											{
												ServerLogger::Log(logid, "Could not remove symbolic link at \"" + backuppath + local_curr_os_path + "\" (2). " + systemErrorInfo(), LL_ERROR);
												c_has_error = true;
												break;
											}

											if (!os_create_dir(os_file_prefix(backuppath + local_curr_os_path)))
											{
												if (!os_directory_exists(os_file_prefix(backuppath + local_curr_os_path)))
												{
													ServerLogger::Log(logid, "Creating directory  \"" + backuppath + local_curr_os_path + "\" failed. - " + systemErrorInfo(), LL_ERROR);
													c_has_error = true;
													break;
												}
												else
												{
													ServerLogger::Log(logid, "Directory \"" + backuppath + local_curr_os_path + "\" does already exist.", LL_WARNING);
												}
											}

											std::string metadata_fn_curr = backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + "/" + escape_metadata_fn(cf.name));
											if(!Server->deleteFile(os_file_prefix(metadata_fn_curr)))
											{
												ServerLogger::Log(logid, "Error deleting metadata file \"" + metadata_fn_curr + "\". " + os_last_error_str(), LL_WARNING);
											}
											if (!os_create_dir(os_file_prefix(backuppath_hashes + local_curr_os_path)))
											{
												ServerLogger::Log(logid, "Error creating metadata directory \"" + backuppath_hashes + local_curr_os_path + "\". " + os_last_error_str(), LL_WARNING);
											}

											metadata_srcpath = last_backuppath_hashes + convertToOSPathFromFileClient(orig_curr_os_path + "/" + escape_metadata_fn(cf.name));
										}
									}
									else
									{
										if(!os_remove_dir(os_file_prefix(metadata_fn)))
										{
											ServerLogger::Log(logid, "Error deleting metadata directory \"" + metadata_fn + "\". " + os_last_error_str(), LL_WARNING);
										}
									}
								}
							}

							if (depth == 0 && curr_path == "/urbackup_backup_scripts")
							{
								metadata.file_permissions = permissionsAllowAll();
								curr_orig_path = local_curr_os_path;
								metadata.orig_path = curr_orig_path;
							}
							
							if( !dir_diff && !indirchange && curr_path!="/urbackup_backup_scripts")
							{
								if(!create_hardlink(os_file_prefix(metadata_fn), os_file_prefix(metadata_srcpath), crossvolume_links, NULL, NULL))
								{
									if(!copy_file(metadata_srcpath, metadata_fn))
									{
										if(client_main->handle_not_enough_space(metadata_fn))
										{
											if(!copy_file(metadata_srcpath, metadata_fn))
											{
												ServerLogger::Log(logid, "Cannot copy directory metadata from \""+metadata_srcpath+"\" to \""+metadata_fn+"\". - " + systemErrorInfo(), LL_ERROR);
											}
										}
										else
										{
											ServerLogger::Log(logid, "Cannot copy directory metadata from \""+metadata_srcpath+"\" to \""+metadata_fn+"\". - " + systemErrorInfo(), LL_ERROR);
										}
									}
								}
							}
							else if(!write_file_metadata(metadata_fn, client_main, metadata, false))
							{
								ServerLogger::Log(logid, "Writing directory metadata to \""+metadata_fn+"\" failed.", LL_ERROR);
								c_has_error=true;
								break;
							}
						}
						
						folder_files.push(std::set<std::string>());
						folder_items.push_back(0);
						dir_ids.push(line);

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
								server_download->addToQueueStartShadowcopy(t);

								continuous_sequences[cf.name]=SContinuousSequence(
									watoi64(extra_params["sequence_id"]), watoi64(extra_params["sequence_next"]));
							}							
						}
					}
					else //cf.name==".."
					{
						if((indirchange || dir_diff_stack.top()) && client_main->getProtocolVersions().file_meta>0 && !script_dir)
						{
							server_download->addToQueueFull(line, ExtractFileName(curr_path, "/"), ExtractFileName(curr_os_path, "/"),
								ExtractFilePath(curr_path, "/"), ExtractFilePath(curr_os_path, "/"), queue_downloads?0:-1,
								metadata, false, true, folder_items.back(), std::string());

							dir_end_ids[dir_ids.top()] = line;
						}

						folder_files.pop();
						folder_items.pop_back();
						dir_diff_stack.pop();
						dir_ids.pop();

						--depth;
						if(indirchange==true && depth==changelevel)
						{
							indirchange=false;
						}
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
				else //is file
				{
					std::string local_curr_os_path=convertToOSPathFromFileClient(curr_os_path+"/"+osspecific_name);
					std::string srcpath=last_backuppath+local_curr_os_path;

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

					bool copy_curr_file_entry=false;
					bool readd_curr_file_entry_sparse=false;
					std::string curr_sha2;
					{
						std::map<std::string, std::string>::iterator hash_it = 
							( (local_hash.get()==NULL)?extra_params.end():extra_params.find(sha_def_identifier) );

						if (local_hash.get() != NULL && hash_it == extra_params.end())
						{
							hash_it = extra_params.find("thash");
						}

						if(hash_it!=extra_params.end())
						{
							curr_sha2 = base64_decode_dash(hash_it->second);
						}

						if (curr_sha2.empty()
							&& phash_load.get() != NULL
							&& !script_dir
							&& extra_params.find("sym_target")==extra_params.end()
							&& extra_params.find("special") == extra_params.end()
							&& !phash_load_offline
							&& extra_params.find("no_hash")==extra_params.end()
							&& cf.size >= link_file_min_size)
						{
							if (!phash_load->getHash(line, curr_sha2))
							{
								ServerLogger::Log(logid, "Error getting parallel hash for file \"" + cf.name + "\" line " + convert(line)+" (2)", LL_ERROR);
								r_offline = true;
								server_download->queueSkip();
								phash_load_offline = true;
							}
							else
							{
								metadata.shahash = curr_sha2;
							}
						}
					}

                    bool download_metadata=false;
					bool write_file_metadata = false;

					bool file_changed = hasChange(line, diffs);

					str_map::iterator sym_target = extra_params.find("sym_target");
					if(sym_target!=extra_params.end() && (indirchange || file_changed || !use_snapshots) )
					{
						std::string symlink_path = backuppath+local_curr_os_path;

						if (use_snapshots && !reflink_files)
						{
							Server->deleteFile(os_file_prefix(symlink_path));
						}

						if(!createSymlink(symlink_path, depth, sym_target->second, (orig_sep), false))
						{
							ServerLogger::Log(logid, "Creating symlink at \""+symlink_path+"\" to \""+sym_target->second+"\" failed. " + systemErrorInfo(), LL_ERROR);
							c_has_error=true;
							break;
						}
                        else
                        {
                            download_metadata=true;
							write_file_metadata = true;
                        }
					}
					else if(extra_params.find("special")!=extra_params.end() && (indirchange || file_changed || !use_snapshots) )
					{
						std::string touch_path = backuppath+local_curr_os_path;
						std::auto_ptr<IFile> touch_file(Server->openFile(os_file_prefix(touch_path), MODE_WRITE));
						if(touch_file.get()==NULL)
						{
							ServerLogger::Log(logid, "Error touching file at \""+touch_path+"\". " + systemErrorInfo(), LL_ERROR);
							c_has_error=true;
							break;
						}
						else
						{
							download_metadata=true;
							write_file_metadata = true;
						}
					}
					else if(indirchange || file_changed) //is changed
					{
						bool f_ok=false;
						if(!curr_sha2.empty() && cf.size>= link_file_min_size)
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
								for(size_t j=0;j<folder_items.size();++j)
								{
									++folder_items[j];
								}

								if(intra_file_diffs)
								{
									server_download->addToQueueChunked(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir, curr_sha2);
								}
								else
								{
									server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir, false, 0, curr_sha2);
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
						bool b=create_hardlink(os_file_prefix(backuppath+local_curr_os_path), os_file_prefix(srcpath), crossvolume_links, &too_many_hardlinks, NULL);

						if(b)
						{
							b = create_hardlink(os_file_prefix(backuppath_hashes+local_curr_os_path), os_file_prefix(last_backuppath_hashes+local_curr_os_path), crossvolume_links, &too_many_hardlinks, NULL);

							if(!b)
							{
								Server->deleteFile(os_file_prefix(backuppath+local_curr_os_path));
							}
						}

						bool f_ok = false;
						if(b)
						{
							f_ok=true;
						}
						else if(!b && too_many_hardlinks)
						{
							ServerLogger::Log(logid, "Creating hardlink from \""+srcpath+"\" to \""+backuppath+local_curr_os_path+"\" failed. Hardlink limit was reached. Copying file...", LL_DEBUG);
							copyFile(line, srcpath, backuppath+local_curr_os_path,
								last_backuppath_hashes+local_curr_os_path,
								backuppath_hashes+local_curr_os_path,
								metadata);
							f_ok=true;
						}

						if(!f_ok) //creating hard link failed and not because of too many hard links per inode
						{
							if(link_logcnt<5)
							{
								ServerLogger::Log(logid, "Creating hardlink from \""+srcpath+"\" to \""+backuppath+local_curr_os_path+"\" failed. "+os_last_error_str()+". Loading file...", LL_WARNING);
							}
							else
							{
								if (link_logcnt == 5)
								{
									ServerLogger::Log(logid, "More warnings of kind: Creating hardlink from \"" + srcpath + "\" to \"" + backuppath + local_curr_os_path + "\" failed. Loading file... Skipping.", LL_WARNING);
								}
								Server->Log("Creating hardlink from \""+srcpath+"\" to \""+backuppath+local_curr_os_path+"\" failed. "+os_last_error_str()+". Loading file...", LL_WARNING);
							}
							++link_logcnt;

							if(!curr_sha2.empty() && cf.size>= link_file_min_size)
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
								for(size_t j=0;j<folder_items.size();++j)
								{
									++folder_items[j];
								}

								if(intra_file_diffs)
								{
									server_download->addToQueueChunked(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir, curr_sha2);
								}
								else
								{
									server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?cf.size:-1,
										metadata, script_dir, false, 0, curr_sha2);
								}
							}
						}
						else //created hard link successfully
						{
							copy_curr_file_entry=copy_last_file_entries;						
							readd_curr_file_entry_sparse = readd_file_entries_sparse;
						}
					}
					else //use_snapshot
					{
						copy_curr_file_entry=copy_last_file_entries;
						readd_curr_file_entry_sparse = readd_file_entries_sparse;
					}

					if(copy_curr_file_entry)
					{
						ServerFilesDao::SFileEntry fileEntry = filesdao->getFileEntryFromTemporaryTable(srcpath);

						if (fileEntry.exists)
						{
							addFileEntrySQLWithExisting(backuppath + local_curr_os_path, backuppath_hashes + local_curr_os_path,
								fileEntry.shahash, fileEntry.filesize, fileEntry.filesize, incremental_num);
							++num_copied_file_entries;

							readd_curr_file_entry_sparse = false;
						}
					}

					if(readd_curr_file_entry_sparse)
					{
						addSparseFileEntry(curr_path, cf, copy_file_entries_sparse_modulo, incremental_num,
							local_curr_os_path, num_readded_entries);
					}

                    if(download_metadata && client_main->getProtocolVersions().file_meta>0)
                    {
						for(size_t j=0;j<folder_items.size();++j)
						{
							++folder_items[j];
						}

                        server_download->addToQueueFull(line, cf.name, osspecific_name, curr_path, curr_os_path, queue_downloads?0:-1,
                            metadata, script_dir, true, 0, std::string(), false, 0, std::string(), write_file_metadata);
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

		if(c_has_error)
			break;

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
			int64 done_bytes = fc.getReceivedDataBytes(true)
				+ (fc_chunked.get() ? fc_chunked->getReceivedDataBytes(true) : 0) + linked_bytes;
			ServerStatus::setProcessDoneBytes(clientname, status_id, done_bytes);
			ServerStatus::setProcessPcDone(clientname, status_id,
				(std::min)(100,(int)(((float)done_bytes)/((float)files_size/100.f)+0.5f)) );
		}

		ServerStatus::setProcessQueuesize(clientname, status_id,
			(_u32)hashpipe->getNumElements(), (_u32)hashpipe_prepare->getNumElements());

		int64 ctime = Server->getTimeMS();
		if(ctime-last_eta_update>eta_update_intervall)
		{
			calculateEtaFileBackup(last_eta_update, eta_set_time, ctime, fc, fc_chunked.get(), linked_bytes, last_eta_received_bytes, eta_estimated_speed, files_size);
		}

		calculateDownloadSpeed(ctime, fc, fc_chunked.get());
	}

	ServerStatus::setProcessSpeed(clientname, status_id, 0);

	if(server_download->isOffline() && !r_offline)
	{
		ServerLogger::Log(logid, "Client "+clientname+" went offline.", LL_ERROR);
		r_offline=true;
	}

	if(incr_backup_stoptime==0)
	{
		incr_backup_stoptime=Server->getTimeMS();
	}

	if (server_download->getHasDiskError()
		&& !server_settings->getSettings()->ignore_disk_errors )
	{
		r_offline = true;
	}

	sendBackupOkay(!r_offline && !c_has_error && !disk_error);

	if(r_offline && server_download->hasTimeout() && !server_download->shouldBackoff() )
	{
		ServerLogger::Log(logid, "Client had timeout. Retrying backup soon...", LL_INFO);
		should_backoff=false;
		has_timeout_error=true;
	}

	num_issues += server_download->getNumIssues();

	ServerLogger::Log(logid, "Waiting for file hashing and copying threads...", LL_INFO);

	waitForFileThreads();

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

	if (!r_offline && !c_has_error && !disk_error)
	{
		if (!loadWindowsBackupComponentConfigXml(fc))
		{
			r_offline = true;
		}
	}

	stopFileMetadataDownloadThread(false, server_download->getNumEmbeddedMetadataFiles());

	if (!r_offline && !c_has_error && !disk_error
		&& client_main->getProtocolVersions().wtokens_version > 0)
	{
		getTokenFile(fc, hashed_transfer, true);
	}

	server_download->deleteTempFolder();

	ServerLogger::Log(logid, "Writing new file list...", LL_INFO);

	Server->deleteFile(clientlistName(backupid));
	IFile* clientlist = Server->openFile(clientlistName(backupid), MODE_WRITE);
	ScopedDeleteFile clientlist_delete(clientlist);

	if(clientlist==NULL)
	{
		ServerLogger::Log(logid, "Error creating client file list for client "+clientname+" at "+Server->getServerWorkingDir() + "/" + clientlistName(backupid) + ". "+os_last_error_str(), LL_ERROR);
		disk_error=true;
	}
	else
	{
		download_nok_ids.finalize();

		bool has_all_metadata=true;

		tmp_filelist->Seek(0);
		line = 0;
		size_t output_offset=0;
		std::stack<size_t> last_modified_offsets;
		list_parser.reset();
		script_dir=false;
		indirchange=false;
		has_read_error = false;
		while( (read=tmp_filelist->Read(buffer, 4096, &has_read_error))>0 )
		{
			if (has_read_error)
			{
				break;
			}
			for(size_t i=0;i<read;++i)
			{
				str_map extra_params;
				bool b=list_parser.nextEntry(buffer[i], cf, &extra_params);
				if(b)
				{
					if(cf.isdir)
					{
						if(!indirchange && hasChange(line, diffs) )
						{
							indirchange=true;
							changelevel=depth;

							if(cf.name=="..")
							{
								--changelevel;
							}
						}

						if(cf.name!="..")
						{
							if(cf.name=="urbackup_backup_scripts")
							{
								script_dir=true;
							}

							int64 end_id = dir_end_ids[line];
							if( !script_dir
								&& metadata_download_thread.get()!=NULL
								&& (indirchange || hasChange(line, dir_diffs))
								&& !metadata_download_thread->hasMetadataId(end_id+1))
							{
								has_all_metadata=false;

								Server->Log("Metadata of \"" + cf.name + "\" is missing", LL_DEBUG);

								if(cf.last_modified==0)
								{
									cf.last_modified+=1;
								}

								cf.last_modified *= Server->getRandomNumber();
							}
							++depth;
						}
						else
						{
							--depth;
							if(indirchange && depth==changelevel)
							{
								indirchange=false;
							}

							script_dir=false;
						}


						writeFileItem(clientlist, cf);
					}
					else if( (extra_params.find("special") != extra_params.end()
								|| extra_params.find("sym_target") != extra_params.end() )
						|| ( server_download->isDownloadOk(line)
							 && !download_nok_ids.hasId(line) ) )
					{
						bool is_special = (extra_params.find("special") != extra_params.end()
							|| extra_params.find("sym_target") != extra_params.end());

						bool metadata_missing = (!script_dir
							&& metadata_download_thread.get()!=NULL
							&& (indirchange || hasChange(line, diffs) || (is_special && !use_snapshots) )
							&& !metadata_download_thread->hasMetadataId(line+1));

						if(metadata_missing)
						{
							Server->Log("Metadata of \"" + cf.name + "\" is missing", LL_DEBUG);
							has_all_metadata=false;
						}

						if(metadata_missing
							|| server_download->isDownloadPartial(line) )
						{
							if(cf.last_modified==0)
							{
								cf.last_modified+=1;
							}

							cf.last_modified *= Server->getRandomNumber();
						}

						writeFileItem(clientlist, cf);
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
	}	

	if(copy_last_file_entries || readd_file_entries_sparse)
	{
		if(num_readded_entries>0)
		{
			ServerLogger::Log(logid, "Number of re-added file entries is "+convert(num_readded_entries), LL_INFO);
		}

		if(num_copied_file_entries>0)
		{
			ServerLogger::Log(logid, "Number of copied file entries from last backup is "+convert(num_copied_file_entries), LL_INFO);
		}

		if (copy_last_file_entries)
		{
			filesdao->dropTemporaryLastFilesTableIndex();
			filesdao->dropTemporaryLastFilesTable();
		}
	}

	if(!r_offline && !c_has_error && !disk_error)
	{
		if(server_settings->getSettings()->end_to_end_file_backup_verification
			|| (client_main->isOnInternetConnection()
			&& server_settings->getSettings()->verify_using_client_hashes 
			&& server_settings->getSettings()->internet_calculate_filehashes_on_client) )
		{
			if(!verify_file_backup(tmp_filelist))
			{
				ServerLogger::Log(logid, "Backup verification failed", LL_ERROR);
				c_has_error=true;
			}
			else
			{
				ServerLogger::Log(logid, "Backup verification ok", LL_INFO);
			}
		}

		if(!c_has_error)
		{
			FileIndex::flush();
			clientlist_delete.release();

			ServerLogger::Log(logid, "Syncing file system...", LL_DEBUG);

			clientlist->Sync();
			Server->destroy(clientlist);

			if (!os_sync(backuppath)
				|| !os_sync(backuppath_hashes))
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
				&& make_subvolume_readonly)
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
				ServerLogger::Log(logid, "Error creating sync file at " + backuppath_hashes + os_file_sep() + sync_fn+". Not setting backup to done.", LL_ERROR);
				c_has_error = true;
			}

			if (!c_has_error
				&& ServerCleanupThread::isClientlistDeletionAllowed())
			{
				Server->deleteFile(clientlist_name);
			}
		}

		if(group==c_group_default || group==c_group_continuous )
		{
			std::string name = "current";
			if(group==c_group_continuous)
			{
				name = "continuous";
			}

			ServerLogger::Log(logid, "Creating symbolic links. -1", LL_DEBUG);

			std::string backupfolder=server_settings->getSettings()->backupfolder;
			std::string currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+name;

			os_remove_symlink_dir(os_file_prefix(currdir));		
			os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));
		}

		if(group==c_group_default)
		{
			ServerLogger::Log(logid, "Creating symbolic links. -2", LL_DEBUG);

			std::string backupfolder=server_settings->getSettings()->backupfolder;
			std::string currdir=backupfolder+os_file_sep()+"clients";
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

				createUserViews(tmp_filelist);
			}

			saveUsersOnClient();
		}
	}
	else if(!c_has_error && !disk_error)
	{
		ServerLogger::Log(logid, "Client disconnected while backing up. Copying partial file...", LL_DEBUG);

		FileIndex::flush();
		clientlist_delete.release();

		ServerLogger::Log(logid, "Syncing file system...", LL_DEBUG);

		clientlist->Sync();
		Server->destroy(clientlist);

		if (!os_sync(backuppath)
			|| !os_sync(backuppath_hashes))
		{
			ServerLogger::Log(logid, "Syncing file system failed. Backup may not be completely on disk. " + os_last_error_str(), BackupServer::canSyncFs() ? LL_ERROR : LL_DEBUG);

			if (BackupServer::canSyncFs())
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
			&& make_subvolume_readonly)
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
			ServerLogger::Log(logid, "Error creating sync file at " + backuppath_hashes + os_file_sep() + sync_fn+". Not setting backup to done", LL_ERROR);
			c_has_error = true;
		}

		if (!c_has_error
			&& ServerCleanupThread::isClientlistDeletionAllowed())
		{
			Server->deleteFile(clientlist_name);
		}
	}
	else
	{
		if (phash_load.get() != NULL)
		{
			std::auto_ptr<IFile> tf(Server->openTemporaryFile());
			if (tf.get() != NULL)
			{
				if(copy_file(tmp_filelist, tf.get()))
				{
					ServerLogger::Log(logid, "Copied file list to "+tf->getFilename()+" for debugging.", LL_ERROR);
				}				
			}
		}

		ServerLogger::Log(logid, "Fatal error during backup. Backup not completed", LL_ERROR);
		ClientMain::sendMailToAdmins("Fatal error occurred during incremental file backup", ServerLogger::getWarningLevelTextLogdata(logid));
	}

	running_updater->stop();
	backup_dao->updateFileBackupRunning(backupid);

	_i64 transferred_bytes=fc.getTransferredBytes()+(fc_chunked.get()?fc_chunked->getTransferredBytes():0);
	_i64 transferred_compressed=fc.getRealTransferredBytes()+(fc_chunked.get()?fc_chunked->getRealTransferredBytes():0);
	int64 passed_time=incr_backup_stoptime-incr_backup_starttime;
	ServerLogger::Log(logid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );
	if(transferred_compressed>0)
	{
		ServerLogger::Log(logid, "(Before compression: "+PrettyPrintBytes(transferred_compressed)+" ratio: "+convert((float)transferred_compressed/transferred_bytes)+")");
	}
	if (linked_bytes > 0)
	{
		ServerLogger::Log(logid, PrettyPrintBytes(linked_bytes) + " of files were already present on the server and did not need to be transferred", LL_INFO);
	}

	if (!ClientMain::run_script("urbackup" + os_file_sep() + "post_incr_filebackup",
		"\"" + backuppath + "\" " + ((c_has_error || r_offline || disk_error) ? "0" : "1") + " " + convert(group), logid))
	{
		c_has_error = true;
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

bool IncrFileBackup::deleteFilesInSnapshot(const std::string clientlist_fn, const std::vector<size_t> &deleted_ids,
	std::string snapshot_path, bool no_error, bool hash_dir, std::vector<size_t>* deleted_inplace_ids)
{
	if(os_directory_exists(os_file_prefix(backuppath + os_file_sep() + "user_views")))
	{
		os_remove_nonempty_dir(os_file_prefix(backuppath + os_file_sep() + "user_views"));
	}

	FileListParser list_parser;

	std::auto_ptr<IFile> tmp(Server->openFile(clientlist_fn, MODE_READ));
	if(tmp.get()==NULL)
	{
		ServerLogger::Log(logid, "Could not open clientlist in ::deleteFilesInSnapshot", LL_ERROR);
		return false;
	}

	char buffer[4096];
	size_t read;
	SFile curr_file;
	size_t line=0;
	std::string curr_path=snapshot_path;
	std::string curr_os_path=snapshot_path;
	bool curr_dir_exists=true;
	std::stack<std::set<std::string> > folder_files;
	folder_files.push(std::set<std::string>());

	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			if(list_parser.nextEntry(buffer[i], curr_file, NULL))
			{
				if(curr_file.isdir && curr_file.name=="..")
				{
					folder_files.pop();
					curr_path=ExtractFilePath(curr_path, os_file_sep());
					curr_os_path=ExtractFilePath(curr_os_path, os_file_sep());
					if(!curr_dir_exists)
					{
						curr_dir_exists=os_directory_exists(curr_path);
					}
				}

				std::string osspecific_name;

				if(!curr_file.isdir || curr_file.name!="..")
				{
					std::string cname = hash_dir ? escape_metadata_fn(curr_file.name) : curr_file.name;
					osspecific_name = fixFilenameForOS(cname, folder_files.top(), curr_path, false, logid, filepath_corrections);
				}

				if( hasChange(line, deleted_ids) 
					&& ( (deleted_inplace_ids ==NULL || !hasChange(line, *deleted_inplace_ids) ) 
						|| (!curr_file.isdir && curr_dir_exists && curr_path == snapshot_path + os_file_sep() + "urbackup_backup_scripts" )
						) )
				{					
					std::string curr_fn=convertToOSPathFromFileClient(curr_os_path+os_file_sep()+osspecific_name);
					if(curr_file.isdir)
					{
						if(curr_dir_exists)
						{
							//In the hash snapshot a symlinked directory is represented by a file
							if ( hash_dir && (os_get_file_type(os_file_prefix(curr_fn)) & EFileType_File) )
							{
								if (!Server->deleteFile(os_file_prefix(curr_fn)))
								{
									ServerLogger::Log(logid, "Could not remove file \"" + curr_fn + "\" in ::deleteFilesInSnapshot - " + systemErrorInfo(), no_error ? LL_WARNING : LL_ERROR);

									if (!no_error)
									{
										return false;
									}
								}
							}
							else if(!os_remove_nonempty_dir(os_file_prefix(curr_fn))
								|| os_directory_exists(os_file_prefix(curr_fn)) )
							{
								ServerLogger::Log(logid, "Could not remove directory \"" + curr_fn + "\" in ::deleteFilesInSnapshot - " + systemErrorInfo(), no_error ? LL_WARNING : LL_ERROR);

								if(!no_error)
								{
									return false;
								}
							}
						}
						curr_path+=os_file_sep()+curr_file.name;
						curr_os_path+=os_file_sep()+osspecific_name;
						curr_dir_exists=false;
						folder_files.push(std::set<std::string>());
					}
					else
					{
						if( curr_dir_exists )
						{
							int ftype = EFileType_File;
							bool keep_inplace = false;
							if (curr_path == snapshot_path + os_file_sep() + "urbackup_backup_scripts")
							{
								ftype = os_get_file_type(os_file_prefix(curr_fn));

								if (ftype & EFileType_File
									&& !hash_dir
									&& (deleted_inplace_ids == NULL || !hasChange(line, *deleted_inplace_ids) ) )
								{
									keep_inplace = true;
								}
							}

							if(ftype & EFileType_File && !keep_inplace
								&& !Server->deleteFile(os_file_prefix(curr_fn)) )
							{
								std::auto_ptr<IFile> tf(Server->openFile(os_file_prefix(curr_fn), MODE_READ));
								if(tf.get()!=NULL)
								{
									ServerLogger::Log(logid, "Could not remove file \""+curr_fn+"\" in ::deleteFilesInSnapshot - " + systemErrorInfo(), no_error ? LL_WARNING : LL_ERROR);
								}
								else
								{
									ServerLogger::Log(logid, "Could not remove file \""+curr_fn+"\" in ::deleteFilesInSnapshot - " + systemErrorInfo()+". It was already deleted.", no_error ? LL_WARNING : LL_ERROR);
								}

								if(!no_error)
								{
									return false;
								}
							}
							else if (ftype & EFileType_Directory
								&& (!os_remove_nonempty_dir(os_file_prefix(curr_fn))
									|| os_directory_exists(os_file_prefix(curr_fn)) ) )
							{
								ServerLogger::Log(logid, "Could not remove directory \"" + curr_fn + "\" in ::deleteFilesInSnapshot (2) - " + systemErrorInfo(), no_error ? LL_WARNING : LL_ERROR);

								if (!no_error)
								{
									return false;
								}
							}
							else if (ftype==0)
							{
								ServerLogger::Log(logid, "Cannot get file type in ::deleteFilesInSnapshot. " + systemErrorInfo(), no_error ? LL_WARNING : LL_ERROR);
								if (!no_error)
								{
									return false;
								}
							}
						}
					}
				}
				else if( curr_file.isdir && curr_file.name!=".." )
				{
					curr_path+=os_file_sep()+curr_file.name;
					curr_os_path+=os_file_sep()+osspecific_name;
					folder_files.push(std::set<std::string>());
				}
				++line;
			}
		}
	}

	return true;
}

void IncrFileBackup::addFileEntrySQLWithExisting( const std::string &fp, const std::string &hash_path, const std::string &shahash, _i64 filesize, _i64 rsize, int incremental)
{
	bool update_fileindex = false;
	int64 entryid = 0;
	int last_entry_clientid = 0;
	int64 next_entry = 0;
	
	if (filesize >= link_file_min_size)
	{
		entryid = fileindex->get_with_cache_exact(FileIndex::SIndexKey(shahash.c_str(), filesize, clientid));

		if (entryid == 0)
		{
			Server->Log("File entry with filesize=" + convert(filesize) 
				+ " hash="+base64_encode(reinterpret_cast<const unsigned char*>(shahash.c_str()), bytes_in_index)
				+ " to file with path \"" + fp + "\" should exist but does not.", LL_DEBUG);

			entryid = fileindex->get_with_cache_prefer_client(FileIndex::SIndexKey(shahash.c_str(), filesize, clientid));

			update_fileindex = true;
		}

		if (entryid != 0)
		{
			ServerFilesDao::SFindFileEntry fentry = filesdao->getFileEntry(entryid);
			if (!fentry.exists)
			{
				Server->Log("File entry in database with id=" + convert(entryid) 
					+ " filesize=" + convert(filesize) 
					+ " hash=" + base64_encode(reinterpret_cast<const unsigned char*>(shahash.c_str()), bytes_in_index)
					+ " to file with path \"" + fp + "\" should exist but does not. -2", LL_WARNING);
				update_fileindex = true;
				entryid = 0;
			}
			else
			{
				last_entry_clientid = fentry.clientid;
				next_entry = fentry.next_entry;
				if (rsize < 0)
				{
					rsize = fentry.rsize;
				}
			}
		}
	}

	if (update_fileindex)
	{
		rsize = filesize;
	}

	BackupServerHash::addFileSQL(*filesdao, *fileindex.get(), backupid, clientid, incremental, fp, hash_path,
		shahash, filesize, rsize, entryid, last_entry_clientid, next_entry, update_fileindex);
}

void IncrFileBackup::addSparseFileEntry( std::string curr_path, SFile &cf, int copy_file_entries_sparse_modulo, int incremental_num,
	std::string local_curr_os_path, size_t& num_readded_entries )
{
	if(cf.size<c_readd_size_limit)
	{
		return;
	}

	std::string curr_file_path = (curr_path + "/" + cf.name);
	std::string md5 = Server->GenerateBinaryMD5(curr_file_path);
	const int* md5ptr = reinterpret_cast<const int*>(md5.data());
	if( (*md5ptr>=0 ? *md5ptr : -1* *md5ptr ) % copy_file_entries_sparse_modulo == incremental_num % copy_file_entries_sparse_modulo )
	{
		FileMetadata metadata;
		std::auto_ptr<IFile> last_file(Server->openFile(os_file_prefix(backuppath+local_curr_os_path), MODE_READ));
		if(!read_metadata(backuppath_hashes+local_curr_os_path, metadata) || last_file.get()==NULL)
		{
			ServerLogger::Log(logid, "Error adding sparse file entry. Could not read metadata from "+backuppath_hashes+local_curr_os_path, LL_WARNING);
		}
		else
		{
			if(metadata.shahash.empty())
			{
				ServerLogger::Log(logid, "Error adding sparse file entry. Could not read hash from "+backuppath_hashes+local_curr_os_path, LL_WARNING);
			}
			else
			{
				addFileEntrySQLWithExisting(backuppath+local_curr_os_path, backuppath_hashes+local_curr_os_path, 
					metadata.shahash, last_file->Size(), 0, incremental_num);

				++num_readded_entries;
			}			
		}
	}
}

void IncrFileBackup::copyFile(size_t fileid, const std::string& source, const std::string& dest,
	const std::string& hash_src, const std::string& hash_dest,
	const FileMetadata& metadata)
{
	max_file_id.setMinDownloaded(fileid);

	CWData data;
	data.addInt(BackupServerHash::EAction_Copy);
	data.addVarInt(fileid);
	data.addString((source));
	data.addString((dest));
	data.addString((hash_src));
	data.addString((hash_dest));
	metadata.serialize(data);

	hashpipe->Write(data.getDataPtr(), data.getDataSize());
}

bool IncrFileBackup::doFullBackup()
{
	setStopBackupRunning(false);
	active_thread->Exit();

	ServerStatus::stopProcess(clientname, status_id);

	FullFileBackup full_backup(client_main, clientid, clientname, clientsubname, log_action,
		group, use_tmpfiles, tmpfile_path, use_reflink, use_snapshots, server_token, details, scheduled);
	full_backup();

	disk_error = full_backup.hasDiskError();
	has_early_error = full_backup.hasEarlyError();
	backupid = full_backup.getBackupid();
	should_backoff = full_backup.shouldBackoff();
	has_timeout_error = full_backup.hasTimeoutError();

	log_action = LogAction_NoLogging;

	return full_backup.getResult();
}
