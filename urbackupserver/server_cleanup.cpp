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

#ifndef CLIENT_ONLY

#include "server_cleanup.h"
#include "../Interface/Server.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/DatabaseCursor.h"
#include "database.h"
#include "../stringtools.h"
#include "server_settings.h"
#include "../urbackupcommon/os_functions.h"
#include "server_update_stats.h"
#include "server_update.h"
#include "server_status.h"
#include "ClientMain.h"
#include "server.h"
#include "snapshot_helper.h"
#include "apps/cleanup_cmd.h"
#include "dao/ServerCleanupDao.h"
#include "dao/ServerLinkDao.h"
#include "dao/ServerFilesDao.h"
#include "server_dir_links.h"
#include <stdio.h>
#include <algorithm>
#include "create_files_index.h"
#include "WalCheckpointThread.h"
#include <assert.h>
#include <set>

IMutex *ServerCleanupThread::mutex=NULL;
ICondition *ServerCleanupThread::cond=NULL;
bool ServerCleanupThread::update_stats=false;
IMutex *ServerCleanupThread::a_mutex=NULL;
bool ServerCleanupThread::update_stats_interruptible=false;
volatile bool ServerCleanupThread::do_quit=false;
bool ServerCleanupThread::update_stats_disabled = false;
std::set<int> ServerCleanupThread::locked_images;
IMutex* ServerCleanupThread::cleanup_lock_mutex = NULL;
bool ServerCleanupThread::allow_clientlist_deletion = true;

void cleanupLastActs();

const unsigned int min_cleanup_interval=12*60*60;

void ServerCleanupThread::initMutex(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
	a_mutex=Server->createMutex();
	cleanup_lock_mutex = Server->createMutex();
}

void ServerCleanupThread::destroyMutex(void)
{
	Server->destroy(mutex);
	Server->destroy(cond);
	Server->destroy(a_mutex);
	Server->destroy(cleanup_lock_mutex);
}

ServerCleanupThread::ServerCleanupThread(CleanupAction cleanup_action)
	: cleanup_action(cleanup_action), cleanupdao(NULL), backupdao(NULL)
{
}

ServerCleanupThread::~ServerCleanupThread(void)
{
}

void ServerCleanupThread::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if(cleanup_action.action!=ECleanupAction_None)
	{
		cleanupdao.reset(new ServerCleanupDao(db));
		backupdao.reset(new ServerBackupDao(db));
		filesdao.reset(new ServerFilesDao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES)));
		fileindex.reset(create_lmdb_files_index());

		switch(cleanup_action.action)
		{
		case ECleanupAction_DeleteFilebackup:
			deleteFileBackup(cleanup_action.backupfolder, cleanup_action.clientid, cleanup_action.backupid, cleanup_action.force_remove);
			break;
		case ECleanupAction_FreeMinspace:
			{
				logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
				ScopedProcess nightly_cleanup(std::string(), sa_emergency_cleanup, std::string(), logid, false);

				deletePendingClients();
				bool b = do_cleanup(cleanup_action.minspace, cleanup_action.switch_to_wal);
				if(cleanup_action.result!=NULL)
				{
					*(cleanup_action.result)=b;
				}
			} break;
		case ECleanupAction_RemoveUnknown:
			do_remove_unknown();
			break;
		}
		
		cleanupdao.reset();
		backupdao.reset();
		filesdao.reset();
		fileindex.reset();

		Server->destroyDatabases(Server->getThreadID());
		delete this;
		return;
	}

	int64 last_cleanup=0;

#ifndef _DEBUG
	{
		IScopedLock lock(mutex);
		cond->wait(&lock, 60000);

		if(do_quit)
		{
			delete this;
			return;
		}
	}
#endif

	{
		ScopedActiveThread sat;
		ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings");
		if( settings->getValue("autoshutdown", "false")=="true" )
		{
			IScopedLock lock(a_mutex);

			cleanupdao.reset(new ServerCleanupDao(db));
			backupdao.reset(new ServerBackupDao(db));
			filesdao.reset(new ServerFilesDao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES)));
			fileindex.reset(create_lmdb_files_index());

			{
				logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
				ScopedProcess nightly_cleanup(std::string(), sa_nightly_cleanup, std::string(), logid, false);

				deletePendingClients();
				do_cleanup();
			}
			
			cleanupdao.reset();
			backupdao.reset();
			filesdao.reset();
			fileindex.reset();

			setClientlistDeletionAllowed(false);

			if(backup_database() && backup_clientlists() && backup_ident() )
			{
				ren_files_backupfolder();
			}

			setClientlistDeletionAllowed(true);
		}

		if( settings->getValue("download_client", "true")=="true" )
		{
			IScopedLock lock(a_mutex);
			ServerUpdate upd;
			upd.update_client();
		}

		if( settings->getValue("show_server_updates", "true")=="true" )
		{
			IScopedLock lock(a_mutex);
			ServerUpdate upd;
			upd.update_server_version_info();
		}

		Server->destroy(settings);
	}

	while(true)
	{
		Server->clearDatabases(Server->getThreadID());

		{
			IScopedLock lock(mutex);
			if(!update_stats)
			{
				cond->wait(&lock, 3600000);
			}
			if(do_quit)
			{
				delete this;
				return;
			}
			if(update_stats)
			{
				if (!update_stats_disabled)
				{
					lock.relock(NULL);
					Server->Log("Updating statistics...", LL_INFO);

					ScopedActiveThread sat;

					{
						IScopedLock lock(a_mutex);
						ServerUpdateStats sus(false, update_stats_interruptible);
						sus();
					}

					Server->Log("Done updating statistics.", LL_INFO);
				}

				update_stats = false;
			}
		}
		db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		db_results res=db->Read("SELECT strftime('%H','now', 'localtime') AS time");
		if(res.empty())
			Server->Log("Reading time failed!", LL_ERROR);
		else
		{
			int chour=watoi(res[0]["time"]);
			ServerSettings settings(db);
			std::vector<STimeSpan> tw=settings.getCleanupWindow();
			if( ( (!tw.empty() && ServerSettings::isInTimeSpan(tw)) || ( tw.empty() && (chour==3 || chour==4) ) )
				&& Server->getTimeSeconds()-last_cleanup>min_cleanup_interval)
			{
				IScopedLock lock(a_mutex);

				ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings");

				ScopedActiveThread sat;

				if( settings->getValue("download_client", "true")=="true" )
				{
					ServerUpdate upd;
					upd.update_client();
				}

				if( settings->getValue("show_server_updates", "true")=="true" )
				{
					IScopedLock lock(a_mutex);
					ServerUpdate upd;
					upd.update_server_version_info();
				}

				cleanupdao.reset(new ServerCleanupDao(db));
				backupdao.reset(new ServerBackupDao(db));
				filesdao.reset(new ServerFilesDao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES)));
				fileindex.reset(create_lmdb_files_index());

				{
					logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
					ScopedProcess nightly_cleanup(std::string(), sa_nightly_cleanup, std::string(), logid, false);

					deletePendingClients();
					do_cleanup();

					enforce_quotas();
				}

				cleanupdao.reset();
				backupdao.reset();
				filesdao.reset();
				fileindex.reset();

				setClientlistDeletionAllowed(false);

				if(backup_database() && backup_clientlists() && backup_ident() )
				{
					ren_files_backupfolder();
				}

				setClientlistDeletionAllowed(true);

				Server->destroy(settings);

				Server->clearDatabases(Server->getThreadID());

				last_cleanup=Server->getTimeSeconds();
			}
		}
	}

	delete this;
}

void ServerCleanupThread::updateStats(bool interruptible)
{
	IScopedLock lock(mutex);
	update_stats=true;
	update_stats_interruptible=interruptible;
	cond->notify_all();
}

bool ServerCleanupThread::isUpdateingStats()
{
	IScopedLock lock(mutex);
	return update_stats;
}

void ServerCleanupThread::disableUpdateStats()
{
	IScopedLock lock(mutex);
	update_stats_disabled = true;
}

void ServerCleanupThread::enableUpdateStats()
{
	IScopedLock lock(mutex);
	update_stats_disabled = false;
}

void ServerCleanupThread::do_cleanup(void)
{
	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+convert(server_settings.getSettings()->update_stats_cachesize));
	}

	removeerr.clear();
	cleanup_images();
	cleanup_files();

	{
		ServerSettings server_settings(db);
		int64 total_space=os_total_space(server_settings.getSettings()->backupfolder);
		if(total_space!=-1)
		{
			int64 amount=cleanup_amount(server_settings.getSettings()->global_soft_fs_quota, db);
			if(amount<total_space)
			{
				Server->Log("Space to free: "+PrettyPrintBytes(total_space-amount), LL_INFO);
				cleanup_images(total_space-amount);
				cleanup_files(total_space-amount);
			}
		}
		else
		{
			Server->Log("Error getting total used space of backup folder", LL_ERROR);
		}
	}

	Server->Log("Updating statistics...", LL_INFO);
	ServerUpdateStats sus;
	sus();
	Server->Log("Done updating statistics.", LL_INFO);

	cleanup_other();

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+cache_res[0]["cache_size"]);
		db->Write("PRAGMA shrink_memory");
	}
}

bool ServerCleanupThread::do_cleanup(int64 minspace, bool switch_to_wal)
{
	ServerStatus::incrementServerNospcStalled(1);
	IScopedLock lock(a_mutex);

	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+convert(server_settings.getSettings()->update_stats_cachesize));
	}
	
	if(minspace>0)
	{
		Server->Log("Space to free: "+PrettyPrintBytes(minspace), LL_INFO);
	}

	removeerr.clear();
	cleanup_images(minspace);
	cleanup_files(minspace);
	cleanup_images();
	cleanup_files();

	if(switch_to_wal)
	{
		cleanup_other();
	}

	if(switch_to_wal==true)
	{
		db->Write("PRAGMA journal_mode=WAL");
	}

	Server->Log("Updating statistics...", LL_INFO);
	ServerUpdateStats sus;
	sus();
	Server->Log("Done updating statistics.", LL_INFO);

	db->destroyAllQueries();

	ServerSettings settings(db);
	int r=hasEnoughFreeSpace(minspace, &settings);

	ServerStatus::incrementServerNospcStalled(-1);

	bool success=(r==1);

	if(!success)
	{
		ServerStatus::setServerNospcFatal(true);
	}

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+cache_res[0]["cache_size"]);
		db->Write("PRAGMA shrink_memory");
	}

	FileIndex::flush();

	return success;
}

void ServerCleanupThread::do_remove_unknown(void)
{
	ServerSettings settings(db);

	replay_directory_link_journal();

	ServerLinkDao link_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS));

	std::string backupfolder=settings.getSettings()->backupfolder;

	std::vector<ServerCleanupDao::SClientInfo> res_clients=cleanupdao->getClients();

	for(size_t i=0;i<res_clients.size();++i)
	{
		int clientid=res_clients[i].id;
		const std::string& clientname=res_clients[i].name;

		Server->Log("Removing unknown for client \""+clientname+"\"");

		std::vector<ServerCleanupDao::SFileBackupInfo> res_file_backups=cleanupdao->getFileBackupsOfClient(clientid);

		for(size_t j=0;j<res_file_backups.size();++j)
		{
			std::string backup_path=backupfolder+os_file_sep()+clientname+os_file_sep()+res_file_backups[j].path;
			int backupid=res_file_backups[j].id;
			if(!os_directory_exists(backup_path))
			{
				Server->Log("Path for file backup [id="+convert(res_file_backups[j].id)+" path="+res_file_backups[j].path+" clientname="+clientname+"] does not exist. Deleting it from the database.", LL_WARNING);

				removeFileBackupSql(backupid);

			}
		}

		std::vector<ServerCleanupDao::SImageBackupInfo> res_image_backups=cleanupdao->getImageBackupsOfClient(clientid);

		for(size_t j=0;j<res_image_backups.size();++j)
		{
			std::string backup_path=res_image_backups[j].path;

			IFile *tf=Server->openFile(os_file_prefix(backup_path), MODE_READ);
			if(tf==NULL)
			{
				Server->Log("Image backup [id="+convert(res_image_backups[j].id)+" path="+res_image_backups[j].path+" clientname="+clientname+"] does not exist. Deleting it from the database.", LL_WARNING);
				cleanupdao->removeImage(res_image_backups[j].id);
			}
			else
			{
				Server->destroy(tf);
			}
		}

		cleanup_system_images(clientid, clientname, settings);


		std::vector<SFile> files=getFiles(backupfolder+os_file_sep()+clientname, NULL);
		std::vector<ServerCleanupDao::SImageBackupInfo> res_images=cleanupdao->getClientImages(clientid);

		for(size_t j=0;j<files.size();++j)
		{
			SFile cf=files[j];

			if(cf.name=="current")
				continue;

			if(cf.name==".directory_pool")
				continue;

			if(cf.isdir)
			{
				if (cf.name.find("Image") == std::string::npos)
				{
					ServerCleanupDao::CondInt res_id = cleanupdao->findFileBackup(clientid, cf.name);

					if (!res_id.exists)
					{
						Server->Log("File backup \"" + cf.name + "\" of client \"" + clientname + "\" not found in database. Deleting it.", LL_WARNING);
						bool remove_folder = false;
						if (BackupServer::isSnapshotsEnabled())
						{
							if (!SnapshotHelper::removeFilesystem(clientname, cf.name))
							{
								remove_folder = true;
							}
						}
						else
						{
							remove_folder = true;
						}

						if (remove_folder)
						{
							std::string rm_dir = backupfolder + os_file_sep() + clientname + os_file_sep() + cf.name;
							if (!remove_directory_link_dir(rm_dir, link_dao, clientid))
							{
								Server->Log("Could not delete directory \"" + rm_dir + "\"", LL_ERROR);
							}
						}
					}
				}
				else
				{
					std::vector<SFile> image_files = getFiles(backupfolder + os_file_sep() + clientname + os_file_sep() + cf.name);

					bool found_image = false;
					for (size_t l = 0; l < image_files.size(); ++l)
					{
						std::string extension = findextension(image_files[l].name);

						if (extension != "vhd" && extension != "vhdz" && extension != "raw")
							continue;

						found_image = true;

						bool found = false;

						for (size_t k = 0; k<res_images.size(); ++k)
						{
							if (ExtractFileName(res_images[k].path) == image_files[l].name)
							{
								found = true;
								break;
							}
						}

						if (!found)
						{
							Server->Log("Image backup \"" + cf.name + "\" of client \"" + clientname + "\" not found in database. Deleting it.", LL_WARNING);
							
							if (extension == "raw")
							{
								SnapshotHelper::removeFilesystem(clientname, cf.name);
							}
							else
							{
								os_remove_nonempty_dir(os_file_prefix(backupfolder + os_file_sep() + clientname + os_file_sep() + cf.name));
							}
						}
					}

					if (!found_image)
					{
						os_remove_nonempty_dir(os_file_prefix(backupfolder + os_file_sep() + clientname + os_file_sep() + cf.name));
					}
				}
			}
			else
			{
				std::string extension=findextension(cf.name);

				if(extension!="vhd" && extension!="vhdz" && extension!="raw")
					continue;

				bool found=false;

				for(size_t k=0;k<res_images.size();++k)
				{
					if(ExtractFileName(res_images[k].path)==cf.name)
					{
						found=true;
						break;
					}
				}

				if(!found)
				{
					Server->Log("Image backup \""+cf.name+"\" of client \""+clientname+"\" not found in database. Deleting it.", LL_WARNING);
					std::string rm_file=backupfolder+os_file_sep()+clientname+os_file_sep()+cf.name;
					if(!Server->deleteFile(rm_file))
					{
						Server->Log("Could not delete file \""+rm_file+"\"", LL_ERROR);
					}
					if(!Server->deleteFile(rm_file+".mbr"))
					{
						Server->Log("Could not delete file \""+rm_file+".mbr\"", LL_ERROR);
					}
					if(!Server->deleteFile(rm_file+".hash"))
					{
						Server->Log("Could not delete file \""+rm_file+".hash\"", LL_ERROR);
					}
					Server->deleteFile(rm_file+".bitmap");
					Server->deleteFile(rm_file+".cbitmap");
				}
			}
		}
		check_symlinks(res_clients[i], backupfolder);
	}

	Server->Log("Removing dangling file entries...", LL_INFO);

	IQuery* q_backup_ids = db->Prepare("SELECT id FROM backups", false);
	IDatabaseCursor* cur = q_backup_ids->Cursor();
	db_single_result res;

	IDatabase* files_db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);

	files_db->Write("CREATE TEMPORARY TABLE backups (id INTEGER PRIMARY KEY)");

	IQuery* q_insert = files_db->Prepare("INSERT INTO backups (id) VALUES (?)", false);

	bool ok = true;
	while (cur->next(res))
	{
		q_insert->Bind(res["id"]);
		ok &= q_insert->Write();
		q_insert->Reset();
	}

	db->destroyQuery(q_backup_ids);
	files_db->destroyQuery(q_insert);

	if (ok)
	{
		filesdao->removeDanglingFiles();
		Server->Log("Deleted " + convert(files_db->getLastChanges()) + " file entries", LL_INFO);
	}

	files_db->Write("DROP TABLE backups");

	FileIndex::flush();
}

int ServerCleanupThread::hasEnoughFreeSpace(int64 minspace, ServerSettings *settings)
{
	if(minspace!=-1)
	{
		std::string path=settings->getSettings()->backupfolder;
		int64 available_space=os_free_space(os_file_prefix(path));
		if(available_space==-1)
		{
			Server->Log("Error getting free space for path \""+path+"\"", LL_ERROR);
			return -1;
		}
		else
		{
			if(available_space>minspace)
			{
				Server->Log("Enough free space now.", LL_DEBUG);
				return 1;
			}
		}
		Server->Log("Free space: "+PrettyPrintBytes(available_space), LL_DEBUG);
	}
	return 0;
}

bool ServerCleanupThread::deleteAndTruncateFile(std::string path)
{
	if(!Server->deleteFile(os_file_prefix(path)))
	{
		Server->Log("Deleting " + path + " failed. " + os_last_error_str() + " . Truncating it instead.", LL_INFO);
		os_file_truncate(os_file_prefix(path), 0);
		return false;
	}
	return true;
}

bool ServerCleanupThread::deleteImage(std::string clientname, std::string path)
{
	std::string image_extension = findextension(path);

	if (image_extension != "raw")
	{
		bool b = true;
		if (!deleteAndTruncateFile(path))
		{
			b = false;
		}
		if (!deleteAndTruncateFile(path + ".hash"))
		{
			b = false;
		}
		if (!deleteAndTruncateFile(path + ".mbr"))
		{
			b = false;
		}
		deleteAndTruncateFile(path + ".cbitmap");

		if (b && ExtractFileName(ExtractFilePath(path)) != clientname)
		{
			b = os_remove_dir(os_file_prefix(ExtractFilePath(path)));
		}

		return b;
	}
	else
	{
		return SnapshotHelper::removeFilesystem(clientname, ExtractFileName(ExtractFilePath(path)));
	}
}

int ServerCleanupThread::max_removable_incr_images(ServerSettings& settings, int backupid)
{
	int incr_image_num=cleanupdao->getIncrNumImagesForBackup(backupid);
	int min_incr_image_num = static_cast<int>(settings.getSettings()->min_image_incr);

	int max_allowed_del_refs = 0;

	if(min_incr_image_num<incr_image_num)
	{
		max_allowed_del_refs = incr_image_num-min_incr_image_num;
	}

	return max_allowed_del_refs;
}

bool ServerCleanupThread::cleanup_images_client(int clientid, int64 minspace, std::vector<int> &imageids, bool cleanup_only_one)
{
	ServerSettings settings(db, clientid);

	int max_image_full=settings.getSettings()->max_image_full;
	if(minspace!=-1)
	{
		max_image_full=settings.getSettings()->min_image_full;
	}

	std::vector<int> notit;

	int backupid;
	int full_image_num=(int)getImagesFullNum(clientid, backupid, notit);
	Server->Log("Client with id="+convert(clientid)+" has "+convert(full_image_num)+" full image backups max="+convert(max_image_full), LL_DEBUG);
	while(full_image_num>max_image_full
		&& full_image_num>0)
	{
		ServerCleanupDao::SImageBackupInfo res_info=cleanupdao->getImageBackupInfo(backupid);
		ServerCleanupDao::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log("Deleting full image backup ( id="+convert(res_info.id)+", backuptime="+res_info.backuptime+", path="+res_info.path+", letter="+res_info.letter+" ) from client \""+clientname.value+"\" ( id="+convert(clientid)+" ) ...", LL_INFO);
		}

		if (isImageLockedFromCleanup(backupid))
		{
			Server->Log("Backup image is locked from cleanup");
			notit.push_back(backupid);
		}
		else if(findUncompleteImageRef(backupid) )
		{		
			Server->Log("Backup image has dependent image which is not complete");
			notit.push_back(backupid);
		}
		else
		{
			if (!removeImage(backupid, &settings, true, false, true, true))
			{
				notit.push_back(backupid);
			}
			else
			{
				imageids.push_back(backupid);

				if (cleanup_only_one)
				{
					return true;
				}
			}
		}

		int r=hasEnoughFreeSpace(minspace, &settings);
		if( r==-1 || r==1 )
			return true;
				
		full_image_num=(int)getImagesFullNum(clientid, backupid, notit);
	}

	notit.clear();

	int max_image_incr=settings.getSettings()->max_image_incr;
	if(minspace!=-1)
		max_image_incr=settings.getSettings()->min_image_incr;

	int incr_image_num=(int)getImagesIncrNum(clientid, backupid, notit);
	Server->Log("Client with id="+convert(clientid)+" has "+convert(incr_image_num)+" incremental image backups max="+convert(max_image_incr), LL_DEBUG);
	while(incr_image_num>max_image_incr
		&& incr_image_num>0)
	{
		ServerCleanupDao::SImageBackupInfo res_info=cleanupdao->getImageBackupInfo(backupid);
		ServerCleanupDao::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log("Deleting incremental image backup ( id="+convert(res_info.id)+", backuptime="+res_info.backuptime+", path="+res_info.path+", letter="+res_info.letter+" ) from client \""+clientname.value+"\" ( id="+convert(clientid)+" ) ...", LL_INFO);
		}

		if (isImageLockedFromCleanup(backupid))
		{
			Server->Log("Backup image is locked from cleanup");
			notit.push_back(backupid);
		}
		else if (findUncompleteImageRef(backupid))
		{
			Server->Log("Backup image has dependent image which is not complete");
			notit.push_back(backupid);
		}
		else
		{
			if(!removeImage(backupid, &settings, true, false, true, true))
			{
				notit.push_back(backupid);
			}
			else
			{
				imageids.push_back(backupid);

				if (cleanup_only_one)
				{
					return true;
				}
			}
		}

		int r=hasEnoughFreeSpace(minspace, &settings);
		if( r==-1 || r==1 )
			return true;
				
		incr_image_num=(int)getImagesIncrNum(clientid, backupid, notit);
	}

	return false;
}

void ServerCleanupThread::cleanup_images(int64 minspace)
{
	std::vector<ServerCleanupDao::SIncompleteImages> incomplete_images=cleanupdao->getIncompleteImages();
	for(size_t i=0;i<incomplete_images.size();++i)
	{
		if (!isImageLockedFromCleanup(incomplete_images[i].id))
		{
			Server->Log("Deleting incomplete image file \"" + incomplete_images[i].path + "\"...", LL_INFO);
			if (!deleteImage(incomplete_images[i].clientname, incomplete_images[i].path))
			{
				Server->Log("Deleting incomplete image \"" + incomplete_images[i].path + "\" failed.", LL_WARNING);
			}
			cleanupdao->removeImage(incomplete_images[i].id);
		}
	}

	{
		ServerSettings settings(db);

		cleanup_all_system_images(settings);

		int r=hasEnoughFreeSpace(minspace, &settings);
		if( r==-1 || r==1)
			return;
	}

	std::vector<int> res=cleanupdao->getClientsSortImagebackups();
	for(size_t i=0;i<res.size();++i)
	{
		int clientid=res[i];
		
		std::vector<int> imageids;
		if(cleanup_images_client(clientid, minspace, imageids, false))
		{
			if(minspace!=-1)
			{
				return;
			}
		}
	}
}

bool ServerCleanupThread::removeImage(int backupid, ServerSettings* settings, 
	bool update_stat, bool force_remove, bool remove_associated, bool remove_references)
{
	int64 deleted_size_bytes = 0;

	if(update_stat)
	{
		deleted_size_bytes=getImageSize(backupid);
	}


	if(update_stat)
	{
		deleted_size_bytes=getImageSize(backupid);
	}

	bool ret = true;

	if(remove_references)
	{
		assert(settings!=NULL);
		std::vector<ServerCleanupDao::SImageRef> refs=cleanupdao->getImageRefs(backupid);

		for(size_t i=0;i<refs.size();++i)
		{
			if(max_removable_incr_images(*settings, refs[i].id)<=0)
			{
				Server->Log("Cannot delete image because incremental image backups referencing this image are not allowed to be deleted", LL_INFO);
				return false;
			}

			bool b=removeImage(refs[i].id, settings, true, force_remove, remove_associated, remove_references);
			if(!b)
			{
				ret=false;
			}
		}
	}
	else if(!remove_references && !force_remove)
	{
		std::vector<ServerCleanupDao::SImageRef> refs=cleanupdao->getImageRefs(backupid);

		if(!refs.empty())
		{
			Server->Log("Cannot delete image because incremental image backups referencing this image exist", LL_INFO);
			return false;
		}
	}

	if(remove_associated)
	{
		std::vector<int> assoc=cleanupdao->getAssocImageBackups(backupid);
		for(size_t i=0;i<assoc.size();++i)
		{
			int64 is=getImageSize(assoc[i]);
			if(is>0) deleted_size_bytes+=is;
			removeImage(assoc[i], settings, false, force_remove, remove_associated, remove_references);
		}
	}

	ServerStatus::updateActive();

	ServerCleanupDao::CondString res=cleanupdao->getImagePath(backupid);
	ServerCleanupDao::CondString res_clientname = cleanupdao->getImageClientname(backupid);
	if(res.exists && res_clientname.exists)
	{
		Server->Log("Deleting image backup ( id="+convert(backupid)+", path="+res.value+" ) ...", LL_INFO);

		_i64 stat_id;
		if(update_stat)
		{
			cleanupdao->addToImageStats(deleted_size_bytes, backupid);
			stat_id=db->getLastInsertID();
		}

		if( deleteImage(res_clientname.value, res.value) || force_remove )
		{
			db->BeginWriteTransaction();
			cleanupdao->removeImage(backupid);
			cleanupdao->removeImageSize(backupid);
			db->EndTransaction();
		}
		else
		{
			Server->Log("Deleting image backup failed.", LL_INFO);
			ret=false;
		}

		if(update_stat)
		{
			cleanupdao->updateDelImageStats(stat_id);
		}
	}
	else
	{
		ret=false;
	}

	ServerStatus::updateActive();

	return ret;
}

bool ServerCleanupThread::findUncompleteImageRef(int backupid)
{
	std::vector<ServerCleanupDao::SImageRef> refs=cleanupdao->getImageRefs(backupid);

	for(size_t i=0;i<refs.size();++i)
	{
		if( refs[i].complete!=1 || findUncompleteImageRef(refs[i].id) )
			return true;
	}
	return false;
}

size_t ServerCleanupThread::getImagesFullNum(int clientid, int &backupid_top, const std::vector<int> &notit)
{
	std::vector<ServerCleanupDao::SImageLetter> res=cleanupdao->getFullNumImages(clientid);
	std::map<std::string, std::vector<int> > images_ids;
	for(size_t i=0;i<res.size();++i)
	{
		std::string letter=res[i].letter;
		int cid=res[i].id;

		if(std::find(notit.begin(), notit.end(), cid)==notit.end())
		{
			images_ids[letter].push_back(cid);			
		}
	}

	size_t max_nimages=0;
	for(std::map<std::string, std::vector<int> >::iterator iter=images_ids.begin();iter!=images_ids.end();++iter)
	{
		if(iter->second.size()>max_nimages)
		{
			backupid_top=iter->second[0];
			max_nimages=iter->second.size();
		}
	}
	return max_nimages;
}

size_t ServerCleanupThread::getImagesIncrNum(int clientid, int &backupid_top, const std::vector<int> &notit)
{
	std::vector<ServerCleanupDao::SImageLetter> res=cleanupdao->getIncrNumImages(clientid);
	std::map<std::string, std::vector<int> > images_ids;
	for(size_t i=0;i<res.size();++i)
	{
		std::string letter=res[i].letter;
		int cid=res[i].id;

		if(std::find(notit.begin(), notit.end(), cid)==notit.end())
		{
			images_ids[letter].push_back(cid);			
		}
	}

	size_t max_nimages=0;
	for(std::map<std::string, std::vector<int> >::iterator iter=images_ids.begin();iter!=images_ids.end();++iter)
	{
		if(iter->second.size()>max_nimages)
		{
			backupid_top=iter->second[0];
			max_nimages=iter->second.size();
		}
	}
	return max_nimages;
}

bool ServerCleanupThread::cleanup_one_filebackup_client(int clientid, int64 minspace, int& filebid)
{
	ServerSettings settings(db, clientid);

	int max_file_full=settings.getSettings()->max_file_full;
	int max_file_incr=settings.getSettings()->max_file_incr;
	if(minspace!=-1)
	{
		max_file_full=settings.getSettings()->min_file_full;
		max_file_incr=settings.getSettings()->min_file_incr;
	}

	int backupid;
	int full_file_num=(int)getFilesFullNum(clientid, backupid);
	Server->Log("Client with id="+convert(clientid)+" has "+convert(full_file_num)+" full file backups max="+convert(max_file_full), LL_DEBUG);
	while(full_file_num>max_file_full
		&& full_file_num>0)
	{
		ServerCleanupDao::SFileBackupInfo res_info=cleanupdao->getFileBackupInfo(backupid);
		ServerCleanupDao::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log("Deleting full file backup ( id="+convert(res_info.id)+", backuptime="+res_info.backuptime+", path="+res_info.path+" ) from client \""+clientname.value+"\" ( id="+convert(clientid)+" ) ...", LL_INFO);
		}
		bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid);
		filebid=backupid;
				
		Server->Log("Done.", LL_INFO);

		if(b)
		{
			return true;
        }
        			
        full_file_num=(int)getFilesFullNum(clientid, backupid);
	}

	int incr_file_num=(int)getFilesIncrNum(clientid, backupid);
	Server->Log("Client with id="+convert(clientid)+" has "+convert(incr_file_num)+" incremental file backups max="+convert(max_file_incr), LL_DEBUG);
	while(incr_file_num>max_file_incr
		&& incr_file_num>0)
	{
		ServerCleanupDao::SFileBackupInfo res_info=cleanupdao->getFileBackupInfo(backupid);
		ServerCleanupDao::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log("Deleting incremental file backup ( id="+convert(res_info.id)+", backuptime="+res_info.backuptime+", path="+res_info.path+" ) from client \""+clientname.value+"\" ( id="+convert(clientid)+" ) ...", LL_INFO);
		}
		bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid);
		filebid=backupid;

		Server->Log("Done.", LL_INFO);

		if(b)
		{
			return true;
		}
		incr_file_num=(int)getFilesIncrNum(clientid, backupid);
	}

	return false;
}

void ServerCleanupThread::cleanup_files(int64 minspace)
{
	ServerSettings settings(db);

	delete_incomplete_file_backups();

	bool deleted_something=true;
	while(deleted_something)
	{
		deleted_something=false;

		{			
			int r=hasEnoughFreeSpace(minspace, &settings);
			if( r==-1 || r==1 )
					return;
		}

		std::vector<int> res=cleanupdao->getClientsSortFilebackups();
		for(size_t i=0;i<res.size();++i)
		{
			int clientid=res[i];
			
			int filebid;
			if(cleanup_one_filebackup_client(clientid, minspace, filebid))
			{
				ServerSettings settings(db);
				int r=hasEnoughFreeSpace(minspace, &settings);
				if( r==-1 || r==1 )
						return;

				deleted_something=true;
			}
		}
	}

}

size_t ServerCleanupThread::getFilesFullNum(int clientid, int &backupid_top)
{
	std::vector<int> res=cleanupdao->getFullNumFiles(clientid);
	std::vector<int> no_err_res;
	if(!removeerr.empty())
	{
		for(size_t i=0;i<res.size();++i)
		{
			int bid=res[i];
			if(std::find(removeerr.begin(), removeerr.end(), bid)==removeerr.end())
			{
				no_err_res.push_back(res[i]);
			}
		}
	}
	else
	{
		no_err_res=res;
	}
	if(!no_err_res.empty())
	{
		backupid_top=no_err_res[0];
	}
	return no_err_res.size();
}

size_t ServerCleanupThread::getFilesIncrNum(int clientid, int &backupid_top)
{
	std::vector<int> res=cleanupdao->getIncrNumFiles(clientid);
	std::vector<int> no_err_res;
	if(!removeerr.empty())
	{
		for(size_t i=0;i<res.size();++i)
		{
			int bid=res[i];
			if(std::find(removeerr.begin(), removeerr.end(), bid)==removeerr.end())
			{
				no_err_res.push_back(res[i]);
			}
		}
	}
	else
	{
		no_err_res=res;
	}
	if(!no_err_res.empty())
	{
		backupid_top=no_err_res[0];
	}
	return no_err_res.size();
}

bool ServerCleanupThread::deleteFileBackup(const std::string &backupfolder, int clientid, int backupid, bool force_remove)
{
	ServerStatus::updateActive();

	ServerCleanupDao::CondString cond_clientname=cleanupdao->getClientName(clientid);
	if(!cond_clientname.exists)
	{
		Server->Log("Error getting clientname in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}
	std::string &clientname=cond_clientname.value;

	ServerCleanupDao::CondString cond_backuppath=cleanupdao->getFileBackupPath(backupid);

	if(!cond_backuppath.exists)
	{
		Server->Log("Error getting backuppath in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	std::string backuppath=cond_backuppath.value;

	if(backuppath.empty())
	{
		Server->Log("Error backuppath empty in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	if(backupfolder.empty())
	{
		Server->Log("Error backupfolder empty in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	if(clientname.empty())
	{
		Server->Log("Error clientname empty in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	std::string path=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath;
	bool b=false;
	if( BackupServer::isSnapshotsEnabled())
	{
		b=SnapshotHelper::removeFilesystem(clientname, backuppath);

		if(!b)
		{
			ServerLinkDao link_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS));

			b=remove_directory_link_dir(path, link_dao, clientid);

			if(!b && SnapshotHelper::isSubvolume(clientname, backuppath) )
			{
				Server->Log("Deleting directory failed. Trying to truncate all files in subvolume to zero...", LL_ERROR);
				b=truncate_files_recurisve(os_file_prefix(path));

				if(b)
				{
					b=remove_directory_link_dir(path, link_dao, clientid);
				}
			}
		}
	}
	else
	{
		ServerLinkDao link_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS));

		b=remove_directory_link_dir(path, link_dao, clientid);
	}

	bool del=true;
	bool err=false;
	if(!b)
	{
		if(!os_directory_exists(os_file_prefix(path)))
		{
			if(os_directory_exists(os_file_prefix(backupfolder)))
			{
			    del=true;
			}
			Server->Log("Warning: Directory doesn't exist: \""+path+"\"", LL_WARNING);
		}
		else
		{
			del=false;
			removeerr.push_back(backupid);
			Server->Log("Error removing directory \""+path+"\"", LL_ERROR);
			err=true;
		}
	}
	if(os_directory_exists(os_file_prefix(path)) )
	{
		del=false;
		Server->Log("Directory still exists. Deleting backup failed. Path: \""+path+"\"", LL_ERROR);
		err=true;
		removeerr.push_back(backupid);
	}
	if(del || force_remove)
	{
		removeFileBackupSql(backupid);


	}

	ServerStatus::updateActive();
	
	return !err;
}

void ServerCleanupThread::removeClient(int clientid)
{
	std::string clientname=cleanupdao->getClientName(clientid).value;
	Server->Log("Deleting client with id \""+convert(clientid)+"\" name \""+clientname+"\"", LL_INFO);
	std::vector<ServerCleanupDao::SImageBackupInfo> res_images;
	//remove image backups
	do
	{
		res_images=cleanupdao->getClientImages(clientid);

		if(!res_images.empty())
		{
			int backupid=res_images[0].id;
			Server->Log("Removing image with id \""+convert(backupid)+"\"", LL_INFO);
			removeImage(backupid, NULL, true, true, false, false);
		}
	}while(!res_images.empty());

	//remove file backups
	ServerSettings settings(db);
	std::vector<int> res_filebackups;
	do
	{
		res_filebackups=cleanupdao->getClientFileBackups(clientid);

		if(!res_filebackups.empty())
		{
			int backupid=res_filebackups[0];
			Server->Log("Removing file backup with id \""+convert(backupid)+"\"", LL_INFO);
			bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid, true);
			if(b)
				Server->Log("Removing file backup with id \""+convert(backupid)+"\" successfull.", LL_INFO);
			else
				Server->Log("Removing file backup with id \""+convert(backupid)+"\" failed.", LL_ERROR);
		}
	}while(!res_filebackups.empty());

	//Update stats
	{
		ServerUpdateStats sus;
		sus();
	}

	//Remove logentries
	IQuery *q=db->Prepare("DELETE FROM logs WHERE clientid=?", false);
	q->Bind(clientid); q->Write(); q->Reset();
	db->destroyQuery(q);

	//history data
	/*q=db->Prepare("SELECT hist_id FROM clients_hist WHERE id=?", false);
	q->Bind(clientid);
	res=q->Read(); q->Reset();
	db->destroyQuery(q);
	q=db->Prepare("DELETE FROM clients_hist_id WHERE id=?", false);
	for(size_t i=0;i<res.size();++i)
	{
		q->Bind(res[i][L"hist_id"]);
		q->Write();
		q->Reset();
	}
	db->destroyQuery(q);*/
	q=db->Prepare("DELETE FROM clients_hist WHERE id=?", false);
	q->Bind(clientid); q->Write(); q->Reset();
	db->destroyQuery(q);

	//settings
	q=db->Prepare("DELETE FROM settings_db.settings WHERE clientid=?", false);
	q->Bind(clientid); q->Write(); q->Reset();
	db->destroyQuery(q);

	//stats
	q=db->Prepare("DELETE FROM del_stats WHERE clientid=?", false);
	q->Bind(clientid); q->Write(); q->Reset();
	db->destroyQuery(q);

	//client
	q=db->Prepare("DELETE FROM clients WHERE id=?", false);
	q->Bind(clientid); q->Write(); q->Reset();
	db->destroyQuery(q);

	q=db->Prepare("DELETE FROM settings_db.extra_clients WHERE id=?", false);
	q->Bind(clientid); q->Write(); q->Reset();
	db->destroyQuery(q);

	//delete dirs
	os_remove_nonempty_dir(settings.getSettings()->backupfolder+os_file_sep()+clientname);
	Server->deleteFile(settings.getSettings()->backupfolder+os_file_sep()+"clients"+os_file_sep()+clientname);
}

void ServerCleanupThread::deletePendingClients(void)
{
	db_results res=db->Read("SELECT id, name FROM clients WHERE delete_pending=1");
	for(size_t i=0;i<res.size();++i)
	{
		SStatus status=ServerStatus::getStatus(res[i]["name"]);
		if(status.has_status==true)
		{
			Server->Log("Cannot remove client \""+res[i]["name"]+"\" ( with id "+res[i]["id"]+"): Client is online or backup is in progress", LL_WARNING);
			continue;
		}

		removeClient(watoi(res[i]["id"]));
	}
}

int64 ServerCleanupThread::getImageSize(int backupid)
{
	ServerCleanupDao::CondInt64 cond_res=cleanupdao->getImageSize(backupid);
	if(cond_res.exists)
	{
		return cond_res.value;
	}
	return -1;
}

namespace
{
	class BackupProgress : public IDatabase::IBackupProgress
	{
	public:
		BackupProgress(size_t status_id)
			: status_id(status_id),
			last_update(Server->getTimeMS()),
			last_pos(0)
		{

		}

		virtual void backupProgress(int64 pos, int64 total)
		{
			int64 ctime = Server->getTimeMS();
			int64 passed = ctime - last_update;

			if (passed > 0)
			{
				int64 new_bytes = pos - last_pos;
				last_pos = pos;
				last_update = ctime;

				ServerStatus::setProcessDoneBytes(std::string(), status_id, pos, total);
				ServerStatus::setProcessSpeed(std::string(), status_id, static_cast<double>(new_bytes) / passed);
			}
		}

	private:
		size_t status_id;
		int64 last_update;
		int64 last_pos;
	};

	bool copy_db_file(std::string src, std::string dst, IDatabase::IBackupProgress* progress)
	{
		std::auto_ptr<IFile> src_file(Server->openFile(os_file_prefix(src), MODE_READ_DEVICE));
		std::auto_ptr<IFile> dst_file(Server->openFile(os_file_prefix(dst), MODE_WRITE));

		bool copy_ok = true;
		if (src_file.get() != NULL
			&& dst_file.get() != NULL)
		{
			std::vector<char> buf;
			buf.resize(32768);
			size_t cnt = 0;
			int64 done_bytes = 0;
			int64 total_bytes = src_file->Size();
			size_t rc;
			bool has_error = false;
			while ((rc = (_u32)src_file->Read(buf.data(), static_cast<_u32>(buf.size()), &has_error))>0)
			{
				if (has_error)
				{
					break;
				}

				if (rc>0)
				{
					done_bytes += rc;

					dst_file->Write(buf.data(), (_u32)rc, &has_error);

					if (has_error)
					{
						break;
					}

					if (cnt % 32 == 0
						&& progress!=NULL)
					{
						progress->backupProgress(done_bytes, total_bytes);
					}
					++cnt;
				}
			}
			
			if (!has_error)
			{
				copy_ok = dst_file->Sync();
			}
			else
			{
				copy_ok = false;
			}
		}
		else
		{
			copy_ok = false;
		}

		return copy_ok;
	}
}

bool ServerCleanupThread::backup_database(void)
{
	ServerSettings settings(db);

	if(settings.getSettings()->backup_database)
	{
		std::vector<DATABASE_ID> copy_backup_ids;
		copy_backup_ids.push_back(URBACKUPDB_SERVER);
		copy_backup_ids.push_back(URBACKUPDB_SERVER_SETTINGS);
		copy_backup_ids.push_back(URBACKUPDB_SERVER_FILES);
		copy_backup_ids.push_back(URBACKUPDB_SERVER_LINKS);
		copy_backup_ids.push_back(URBACKUPDB_SERVER_LINK_JOURNAL);

		std::vector<std::string> copy_backup;
		copy_backup.push_back("backup_server.db");
		copy_backup.push_back("backup_server_settings.db");
		copy_backup.push_back("backup_server_files.db");
		copy_backup.push_back("backup_server_links.db");
		copy_backup.push_back("backup_server_link_journal.db");

		copy_backup.push_back("backup_server.db-wal");
		copy_backup.push_back("backup_server_settings.db-wal");
		copy_backup.push_back("backup_server_files.db-wal");
		copy_backup.push_back("backup_server_links.db-wal");
		copy_backup.push_back("backup_server_link_journal.db-wal");


		bool integrity_ok = true;
		{
			logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
			ScopedProcess check_integrity(std::string(), sa_check_integrity, std::string(), logid, false);

			for (size_t i = 0; i < copy_backup_ids.size(); ++i)
			{
				if (integrity_ok)
				{
					IDatabase* copy_db = Server->getDatabase(Server->getThreadID(), copy_backup_ids[i]);

					ServerLogger::Log(logid, "Checking integrity of " + copy_backup[i], LL_INFO);

					db_results res = copy_db->Read("PRAGMA quick_check");

					integrity_ok = !res.empty() && res[0]["integrity_check"] == "ok";

					if (!integrity_ok)
					{
						ServerLogger::Log(logid, "Integrity check failed", LL_ERROR);
					}
				}
			}
		}

		if(integrity_ok)
		{
			std::string bfolder=settings.getSettings()->backupfolder+os_file_sep()+"urbackup";
			if(!os_directory_exists(bfolder) )
			{
				os_create_dir(bfolder);
			}

			bool total_copy_ok = true;
			for (size_t i = 0; i < copy_backup_ids.size(); ++i)
			{
				IDatabase* copy_db = Server->getDatabase(Server->getThreadID(), copy_backup_ids[i]);

				logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
				ScopedProcess database_backup(std::string(), sa_backup_database, copy_backup[i], logid, false);

				ServerLogger::Log(logid, "Starting database backup of " + copy_backup[i] + "...", LL_INFO);

				ServerLogger::Log(logid, "Stop checkpointing of " + copy_backup[i] + "...", LL_INFO);

				WalCheckpointThread::lockForBackup("urbackup" + os_file_sep() + copy_backup[i]);

				ServerLogger::Log(logid, "Stop writes to " + copy_backup[i]+"...", LL_INFO);

				DBScopedWriteTransaction copy_db_transaction(copy_db);

				BackupProgress backup_progress(database_backup.getStatusId());

				bool copy_ok = copy_db_file(Server->getServerWorkingDir()+ os_file_sep() + "urbackup" + os_file_sep() + copy_backup[i], bfolder + os_file_sep() + copy_backup[i] + "~", &backup_progress);

				if (copy_ok)
				{
					ServerStatus::setProcessDetails(std::string(), database_backup.getStatusId(), copy_backup[i] + "-wal", -1);

					BackupProgress backup_progress_wal(database_backup.getStatusId());

					copy_ok = copy_db_file(Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + copy_backup[i]+"-wal", bfolder + os_file_sep() + copy_backup[i]+"-wal~", &backup_progress_wal);

					if (!copy_ok)
					{
						ServerLogger::Log(logid, "Backing up database failed. Copying urbackup" + os_file_sep() + copy_backup[i] + "-wal to " + bfolder + os_file_sep() + copy_backup[i] + "-wal~ failed", LL_ERROR);
						total_copy_ok = false;
					}
					else
					{
						ServerLogger::Log(logid, "Backup of " + copy_backup[i] + " done.", LL_INFO);
					}				
				}
				else
				{
					ServerLogger::Log(logid, "Backing up database failed. Copying urbackup" + os_file_sep() + copy_backup[i] + " to " + bfolder + os_file_sep() + copy_backup[i] + "~ failed", LL_ERROR);
					total_copy_ok = false;
				}

				WalCheckpointThread::unlockForBackup("urbackup" + os_file_sep() + copy_backup[i]);
			}	

			return total_copy_ok;
		}
		else
		{
			Server->Log("Database integrity check failed. Skipping Database backup.", LL_ERROR);
			Server->setFailBit(IServer::FAIL_DATABASE_CORRUPTED);
			ClientMain::sendMailToAdmins("Database integrity check failed", "Database integrity check failed before database backup. You should restore the UrBackup database from a backup or try to repair it.");
			return false;
		}
	}

	return true;
}

void ServerCleanupThread::doQuit(void)
{
	do_quit=true;
	cond->notify_all();
}

void ServerCleanupThread::lockImageFromCleanup(int backupid)
{
	IScopedLock lock(cleanup_lock_mutex);
	locked_images.insert(backupid);
}

void ServerCleanupThread::unlockImageFromCleanup(int backupid)
{
	IScopedLock lock(cleanup_lock_mutex);
	std::set<int>::iterator it = locked_images.find(backupid);
	if (it != locked_images.end())
	{
		locked_images.erase(it);
	}
}

bool ServerCleanupThread::isImageLockedFromCleanup(int backupid)
{
	IScopedLock lock(cleanup_lock_mutex);
	return locked_images.find(backupid) != locked_images.end();
}

bool ServerCleanupThread::isClientlistDeletionAllowed()
{
	IScopedLock lock(mutex);
	return allow_clientlist_deletion;
}

bool ServerCleanupThread::truncate_files_recurisve(std::string path)
{
	std::vector<SFile> files=getFiles(path, NULL);

	for(size_t i=0;i<files.size();++i)
	{
		if(files[i].isdir)
		{
			bool b=truncate_files_recurisve(path+os_file_sep()+files[i].name);
			if(!b)
			{
				return false;
			}
		}
		else
		{
			bool b=os_file_truncate(path+os_file_sep()+files[i].name, 0);
			if(!b)
			{
				Server->Log("Truncating file \""+path+os_file_sep()+files[i].name+"\" failed. Stopping truncating.", LL_ERROR);
				return false;
			}
		}
	}
	return true;
}

bool ServerCleanupThread::cleanupSpace(int64 minspace, bool switch_to_wal)
{
	bool result;
	Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(minspace, &result, switch_to_wal)),
		"free space");
	return result;
}

void ServerCleanupThread::removeUnknown(void)
{
	Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(ECleanupAction_RemoveUnknown)),
		"remove unknown");
}

void ServerCleanupThread::enforce_quotas(void)
{
	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+convert(server_settings.getSettings()->update_stats_cachesize));
	}

	std::vector<ServerCleanupDao::SClientInfo> clients=cleanupdao->getClients();

	for(size_t i=0;i<clients.size();++i)
	{
		Server->Log("Enforcing quota for client \"" + (clients[i].name)+ "\" (id="+convert(clients[i].id)+")", LL_INFO);
		std::ostringstream log;
		log << "Quota enforcement report for client \"" << (clients[i].name) << "\" (id=" << clients[i].id  << ")" << std::endl;

		if(!enforce_quota(clients[i].id, log))
		{
			ClientMain::sendMailToAdmins("Quota enforcement failed", log.str());
			Server->Log(log.str(), LL_ERROR);
		}
		else
		{
			Server->Log(log.str(), LL_DEBUG);
		}
	}

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+cache_res[0]["cache_size"]);
		db->Write("PRAGMA shrink_memory");
	}
}

bool ServerCleanupThread::enforce_quota(int clientid, std::ostringstream& log)
{
	ServerSettings client_settings(db, clientid);

	std::string client_quota = trim(client_settings.getSettings()->client_quota);
	if(client_quota.empty() || client_quota=="100%" || client_quota=="-")
	{
		log << "Client does not have a quota or quota is 100%" << std::endl;
		return true;
	}

	bool did_remove_something=false;
	do
	{
		ServerCleanupDao::CondInt64 used_storage=cleanupdao->getUsedStorage(clientid);
		if(!used_storage.exists || used_storage.value<0)
		{
			log << "Error getting used storage of client" << std::endl;
			return false;
		}

		int64 client_quota=cleanup_amount(client_settings.getSettings()->client_quota, db);

		log << "Client uses " << PrettyPrintBytes(used_storage.value) << " and has a quota of " << PrettyPrintBytes(client_quota) << std::endl;

		if(used_storage.value<=client_quota)
		{		
			log << "Client within assigned quota." << std::endl;
			return true;
		}
		else
		{
			log << "This requires enforcement of the quota." << std::endl;
		}

		did_remove_something=false;
		int state=0;
		int nopc=0;
		while(used_storage.value>client_quota && nopc<2)
		{
			std::string path=client_settings.getSettings()->backupfolder;
			int64 available_space=os_free_space(os_file_prefix(path));

			if(available_space==-1)
			{
				log << "Error getting free space -5" << std::endl;
				return false;
			}

			int64 space_to_free=used_storage.value-client_quota;

			int64 target_minspace=available_space+space_to_free;

			if(target_minspace<0)
			{
				log << "Error. Target space is negative" << std::endl;
				return false;
			}

			if(state==0)
			{
				std::vector<int> imageids;
				cleanup_images_client(clientid, target_minspace, imageids, true);
				if(!imageids.empty())
				{
					log << "Removed " << imageids.size() << " image with id ";
					for(size_t i=0;i<imageids.size();++i) log << imageids[i] << " ";
					log << std::endl;

					did_remove_something=true;
					break;
				}
				else
				{
					++nopc;
				}
			}
			else
			{
				int filebid;
				if(cleanup_one_filebackup_client(clientid, target_minspace, filebid))
				{
					log << "Removed file backup with id " << filebid << std::endl;

					did_remove_something=true;
					nopc=0;
					if(hasEnoughFreeSpace(target_minspace, &client_settings))
					{
						break;
					}
				}
				else
				{
					++nopc;
				}
			}
			state=(state+1)%2;
		}

		if(did_remove_something)
		{
			ServerUpdateStats sus(false, false);
			sus();
		}
	}
	while(did_remove_something);

	return false;
}

namespace
{
	std::vector<bool> check_backupfolders_exist(std::vector<std::string>& old_backupfolders)
	{
		std::vector<bool> ret;
		ret.resize(old_backupfolders.size());
		for(size_t i=0;i<old_backupfolders.size();++i)
		{
			ret[i] = os_directory_exists(os_file_prefix(old_backupfolders[i]));			
		}
		return ret;
	}
}

bool ServerCleanupThread::correct_poolname( const std::string& backupfolder, const std::string& clientname, const std::string& pool_name, std::string& pool_path )
{
	const std::string pool_root = backupfolder + os_file_sep() + clientname + os_file_sep() + ".directory_pool";
	pool_path = pool_root + os_file_sep() + pool_name.substr(0, 2) + os_file_sep() + pool_name;

	if(os_directory_exists(pool_path))
	{
		return true;
	}

	static std::vector<std::string> old_backupfolders = backupdao->getOldBackupfolders();
	static std::vector<bool> backupfolders_exist = check_backupfolders_exist(old_backupfolders);

	for(size_t i=0;i<old_backupfolders.size();++i)
	{
		if(backupfolders_exist[i])
		{
			pool_path = old_backupfolders[i] + os_file_sep() + clientname + os_file_sep() + ".directory_pool"
				+ os_file_sep() + pool_name.substr(0, 2) + os_file_sep() + pool_name;

			if(os_directory_exists(pool_path))
			{
				return true;
			}
		}
	}

	return false;
}

void ServerCleanupThread::check_symlinks( const ServerCleanupDao::SClientInfo& client_info, const std::string& backupfolder )
{
	const int clientid=client_info.id;
	const std::string& clientname=client_info.name;
	const std::string pool_root = backupfolder + os_file_sep() + clientname + os_file_sep() + ".directory_pool";
	IDatabase* db_links = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS);
	ServerLinkDao link_dao(db_links);

	std::vector<int64> del_ids;
	std::vector<std::pair<int64, std::string> > target_adjustments;

	IQuery* q_directory_links = db_links->Prepare("SELECT id, name, target FROM directory_links WHERE clientid=?", false);
	q_directory_links->Bind(clientid);

	IDatabaseCursor* cursor = q_directory_links->Cursor();

	db_single_result res;
	while(cursor->next(res))
	{
		const std::string& pool_name = res["name"];
		const std::string& target = res["target"];

		std::string new_target = target;
		std::string pool_path;
		bool exists=true;
		if(!correct_poolname(backupfolder, clientname, pool_name, pool_path) || !correct_target(backupfolder, new_target))
		{
			if(!correct_poolname(backupfolder, clientname, pool_name, pool_path))
			{
				Server->Log("Pool directory for pool entry \""+pool_name+"\" not found.");
			}
			if(!correct_target(backupfolder, new_target))
			{
				Server->Log("Pool directory source symbolic link \""+new_target+"\" not found.");
			}
			Server->Log("Deleting pool reference entry");
			del_ids.push_back(watoi(res["id"]));
			exists=false;
		}
		else if(target!=new_target)
		{
			Server->Log("Adjusting pool directory target from \""+target+"\" to \""+new_target+"\"");
			target_adjustments.push_back(std::make_pair(watoi(res["id"]), new_target));
		}
		
		if(exists)
		{
			std::string symlink_pool_path;
			if(os_get_symlink_target(os_file_prefix(new_target), symlink_pool_path))
			{
				if(symlink_pool_path!=pool_path)
				{
					Server->Log("Correcting target of symlink \""+new_target+"\" to \""+pool_path+"\"");
					if(os_remove_symlink_dir(os_file_prefix(new_target)))
					{
						if(!os_link_symbolic(pool_path, os_file_prefix(new_target)))
						{
							Server->Log("Could not create symlink at \""+new_target+"\" to \""+pool_path+"\"", LL_ERROR);
						}
					}
					else
					{
						Server->Log("Error deleting symlink \""+new_target+"\"", LL_ERROR);
					}
				}
			}
		}
	}
	db_links->destroyQuery(q_directory_links);

	for(size_t i=0;i<del_ids.size();++i)
	{
		link_dao.deleteLinkReferenceEntry(del_ids[i]);
	}

	for(size_t i=0;i<target_adjustments.size();++i)
	{
		link_dao.updateLinkReferenceTarget(target_adjustments[i].second, target_adjustments[i].first);
	}

	if(os_directory_exists(pool_root))
	{
		std::vector<SFile> first_files = getFiles(pool_root, NULL);
		for(size_t i=0;i<first_files.size();++i)
		{
			if(!first_files[i].isdir)
				continue;
			if(first_files[i].issym)
				continue;

			std::string curr_path = pool_root + os_file_sep() + first_files[i].name;
			std::vector<SFile> pool_files = getFiles(curr_path, NULL);

			for(size_t j=0;i<pool_files.size();++i)
			{
				if(!pool_files[i].isdir)
					continue;
				if(pool_files[i].issym)
					continue;

				std::string pool_path = curr_path + os_file_sep() + pool_files[i].name;

				if(link_dao.getDirectoryRefcount(clientid, pool_files[i].name)==0)
				{
					Server->Log("Refcount of \""+pool_path+"\" is zero. Deleting pool folder.");
					if(!remove_directory_link_dir(pool_path, link_dao, clientid))
					{
						Server->Log("Could not remove pool folder \""+pool_path+"\"", LL_ERROR);
					}
				}
			}
		}
	}	
}

bool ServerCleanupThread::correct_target(const std::string& backupfolder, std::string& target)
{
	if(os_is_symlink(os_file_prefix(target)))
	{
		return true;
	}

	static std::vector<std::string> old_backupfolders = backupdao->getOldBackupfolders();

	for(size_t i=0;i<old_backupfolders.size();++i)
	{
		size_t erase_size = old_backupfolders[i].size() + os_file_sep().size();
		if(target.size()>erase_size &&
			next(target, 0, old_backupfolders[i]))
		{
			std::string new_target = backupfolder + os_file_sep() + target.substr(erase_size);

			if(os_is_symlink(os_file_prefix(new_target)))
			{
				target = new_target;
				return true;
			}
		}
	}

	return false;
}

void ServerCleanupThread::delete_incomplete_file_backups( void )
{
	std::vector<ServerCleanupDao::SIncompleteFileBackup> incomplete_file_backups =
		cleanupdao->getIncompleteFileBackups();

	ServerSettings settings(db);

	for(size_t i=0;i<incomplete_file_backups.size();++i)
	{
		const ServerCleanupDao::SIncompleteFileBackup& backup = incomplete_file_backups[i];
		Server->Log("Deleting incomplete file backup ( id="+convert(backup.id)+", backuptime="+backup.backuptime+", path="+backup.path+" ) from client \""+backup.clientname+"\" ( id="+convert(backup.clientid)+" ) ...", LL_INFO);
		if(!deleteFileBackup(settings.getSettings()->backupfolder, backup.clientid, backup.id))
		{
			Server->Log("Error deleting file backup", LL_WARNING);
		}
		else
		{
			Server->Log("done.");
		}
	}
}

void ServerCleanupThread::rewrite_history(const std::string& back_start, const std::string& back_stop, const std::string& date_grouping)
{
	Server->Log("Reading history...", LL_DEBUG);
	std::vector<ServerCleanupDao::SHistItem> daily_history = cleanupdao->getClientHistory(back_start, back_stop, date_grouping);
	Server->Log(convert(daily_history.size()) + " history items read", LL_DEBUG);


	db_results foreign_keys;
	if(db->getEngineName()=="sqlite")
	{
		foreign_keys=db->Read("PRAGMA foreign_keys");
		db->Write("PRAGMA foreign_keys = 0");
	}

	{
		DBScopedDetach detachDbs(db);
		DBScopedWriteTransaction transaction(db);

		Server->Log("Deleting history...", LL_DEBUG);
		cleanupdao->deleteClientHistoryIds(back_start, back_stop);
		cleanupdao->deleteClientHistoryItems(back_start, back_stop);

		Server->Log("Writing history...", LL_DEBUG);
		for(size_t i=0;i<daily_history.size();++i)
		{
			const ServerCleanupDao::SHistItem& hist_item = daily_history[i];
			cleanupdao->insertClientHistoryId(hist_item.max_created);
			int64 hist_id = db->getLastInsertID();
			cleanupdao->insertClientHistoryItem(hist_item.id,
				hist_item.name, hist_item.lastbackup, hist_item.lastseen,
				hist_item.lastbackup_image,
				hist_item.bytes_used_files, hist_item.bytes_used_images,
				hist_item.max_created,
				hist_id);
		}
	}
	
	if(db->getEngineName()=="sqlite" &&
		!foreign_keys.empty() )
	{
		db->Write("PRAGMA foreign_keys = " + foreign_keys[0]["foreign_keys"]);
	}
}

void ServerCleanupThread::cleanup_client_hist()
{
	Server->Log("Rewriting daily history...", LL_INFO);
	rewrite_history("-2 days", "-2 month", "%Y-%m-%d");
	Server->Log("Rewriting monthly history...", LL_INFO);
	rewrite_history("-2 month", "-4 years", "%Y-%m");
	Server->Log("Rewriting yearly history...", LL_INFO);
	rewrite_history("-2 years", "-1000 years", "%Y");
}

void ServerCleanupThread::cleanup_all_system_images(ServerSettings & settings)
{
	std::vector<ServerCleanupDao::SClientInfo> res_clients = cleanupdao->getClients();

	for (size_t i = 0; i < res_clients.size(); ++i)
	{
		int clientid = res_clients[i].id;
		const std::string& clientname = res_clients[i].name;

		cleanup_system_images(clientid, clientname, settings);
	}
}

void ServerCleanupThread::cleanup_system_images(int clientid, std::string clientname, ServerSettings& settings)
{
	std::vector<ServerCleanupDao::SImageBackupInfo>
		res_image_backups = cleanupdao->getOldImageBackupsOfClient(clientid);

	for (size_t j = 0; j<res_image_backups.size(); ++j)
	{
		if (res_image_backups[j].letter == "SYSVOL"
			|| res_image_backups[j].letter == "ESP")
		{
			if (!cleanupdao->getParentImageBackup(res_image_backups[j].id).exists
				&& !isImageLockedFromCleanup(res_image_backups[j].id) )
			{
				Server->Log("Image backup [id=" + convert(res_image_backups[j].id) + " path=" 
					+ res_image_backups[j].path + " clientname=" + clientname 
					+ "] is a system reserved or EFI system partition image older than 24h and has no parent image. Deleting it.", LL_INFO);
				if (!removeImage(res_image_backups[j].id, &settings, false, false, true, false))
				{
					Server->Log("Could not remove image backup [id=" + convert(res_image_backups[j].id) + " path=" + res_image_backups[j].path + " clientname=" + clientname + "]", LL_ERROR);
				}
			}
		}
	}
}

void ServerCleanupThread::cleanup_other()
{
	Server->Log("Deleting old logs...", LL_INFO);
	cleanupdao->cleanupBackupLogs();
	cleanupdao->cleanupAuthLog();
	Server->Log("Done deleting old logs", LL_INFO);

	Server->Log("Cleaning history...", LL_INFO);
	cleanup_client_hist();
	Server->Log("Done cleaning history", LL_INFO);

	Server->Log("Cleaning deleted backups history...", LL_INFO);
	cleanupLastActs();
	Server->Log("Done cleaning deleted backups history.", LL_INFO);

	Server->Log("Cleaning up client lists...", LL_INFO);
	cleanup_clientlists();
	Server->Log("Done cleaning up client lists.", LL_INFO);
}

void ServerCleanupThread::removeFileBackupSql( int backupid )
{
	DBScopedSynchronous synchronous_files(filesdao->getDatabase());
	filesdao->BeginWriteTransaction();

	BackupServerHash::SInMemCorrection correction;

	ServerFilesDao::SBackupIdMinMax minmax = filesdao->getBackupIdMinMax(backupid);

	correction.max_correct = minmax.tmax;
	correction.min_correct = minmax.tmin;

	IQuery* q_iterate = filesdao->getDatabase()->Prepare("SELECT id, shahash, filesize, rsize, clientid, backupid, incremental, next_entry, prev_entry, pointed_to FROM files WHERE backupid=?", false);
	q_iterate->Bind(backupid);
	IDatabaseCursor* cursor = q_iterate->Cursor();

	db_single_result res;
	while(cursor->next(res))
	{
		int64 id = watoi64(res["id"]);

		int64 filesize = watoi64(res["filesize"]);
		int64 rsize = watoi64(res["rsize"]);
		int clientid = watoi(res["clientid"]);
		int backupid = watoi(res["backupid"]);
		int incremental = watoi(res["incremental"]);
		int64 next_entry = watoi64(res["next_entry"]);
		int64 prev_entry = watoi64(res["prev_entry"]);
		int pointed_to = watoi(res["pointed_to"]);

		std::map<int64, int64>::iterator it_next = correction.next_entries.find(id);
		if (it_next != correction.next_entries.end())
		{
			if (it_next->second != next_entry)
			{
				int abc = 5;
			}

			next_entry = it_next->second;

			correction.next_entries.erase(it_next);
		}

		std::map<int64, int64>::iterator it_prev= correction.prev_entries.find(id);
		if (it_prev != correction.prev_entries.end())
		{
			if (it_prev->second != prev_entry)
			{
				int abc = 5;
			}

			prev_entry = it_prev->second;

			correction.prev_entries.erase(it_prev);
		}

		std::map<int64, int>::iterator it_pointed_to = correction.pointed_to.find(id);
		if (it_pointed_to != correction.pointed_to.end())
		{
			if (it_pointed_to->second != pointed_to)
			{
				int abc = 5;
			}

			pointed_to = it_pointed_to->second;

			correction.pointed_to.erase(it_pointed_to);
		}

		BackupServerHash::deleteFileSQL(*filesdao, *fileindex.get(), res["shahash"].c_str(),
			filesize, rsize, clientid, backupid, incremental, id, prev_entry, next_entry, pointed_to, false, false, false, true, &correction);
	}
	filesdao->getDatabase()->destroyQuery(q_iterate);

	for (std::map<int64, int64>::iterator it_next = correction.next_entries.begin();
		 it_next != correction.next_entries.end(); ++it_next)
	{
		filesdao->setNextEntry(it_next->second, it_next->first);
	}

	for (std::map<int64, int64>::iterator it_prev = correction.prev_entries.begin();
		 it_prev != correction.prev_entries.end(); ++it_prev)
	{
		filesdao->setPrevEntry(it_prev->second, it_prev->first);
	}

	for (std::map<int64, int>::iterator it_pointed_to = correction.pointed_to.begin();
		 it_pointed_to != correction.pointed_to.end(); ++it_pointed_to)
	{
		filesdao->setPointedTo(it_pointed_to->second, it_pointed_to->first);
	}

	filesdao->deleteFiles(backupid);

	filesdao->endTransaction();

	cleanupdao->removeFileBackup(backupid);
}

bool ServerCleanupThread::backup_clientlists()
{
	ServerSettings settings(db);

	if(settings.getSettings()->backup_database)
	{
		std::string bfolder=settings.getSettings()->backupfolder+os_file_sep()+"urbackup";

		std::string srcfolder = Server->getServerWorkingDir()+os_file_sep()+"urbackup";
		std::vector<SFile> files = getFiles(srcfolder);

		bool has_error=false;
		for(size_t i=0;i<files.size();++i)
		{
			if(files[i].name.find("clientlist_b_")==0)
			{
				if(!copy_file(os_file_prefix(srcfolder + os_file_sep() + files[i].name), os_file_prefix(bfolder + os_file_sep() + files[i].name+"~"), true))
				{
					Server->Log("Error backing up "+files[i].name, LL_ERROR);
					has_error=true;
				}
			}
		}

		return !has_error;
	}
	else
	{
		return true;
	}
}

void ServerCleanupThread::ren_files_backupfolder()
{
	ServerSettings settings(db);

	std::string bfolder=settings.getSettings()->backupfolder+os_file_sep()+"urbackup";

	if(!os_directory_exists(bfolder) )
	{
		os_create_dir(bfolder);
		return;
	}

	std::vector<SFile> files = getFiles(bfolder);

	for (size_t i = 0; i < files.size(); ++i)
	{
		if (!files[i].isdir && 
			!files[i].name.empty() &&
			files[i].name[files[i].name.size() - 1] != '~')
		{
			Server->deleteFile(os_file_prefix(bfolder + os_file_sep() + files[i].name));
		}
	}

	for(size_t i=0;i<files.size();++i)
	{
		if(!files[i].isdir &&
			!files[i].name.empty() &&
			files[i].name[files[i].name.size()-1]=='~')
		{
			os_rename_file(os_file_prefix(bfolder+os_file_sep()+files[i].name), os_file_prefix(bfolder+os_file_sep()+files[i].name.substr(0, files[i].name.size()-1)));
		}
	}
}

void ServerCleanupThread::setClientlistDeletionAllowed(bool b)
{
	IScopedLock lock(mutex);
	allow_clientlist_deletion = b;
}

bool ServerCleanupThread::backup_ident()
{
	ServerSettings settings(db);

	std::string bfolder=settings.getSettings()->backupfolder+os_file_sep()+"urbackup";

	if (!os_directory_exists(bfolder))
	{
		os_create_dir(bfolder);
	}

	std::string srcfolder = Server->getServerWorkingDir()+os_file_sep()+"urbackup";

	std::vector<std::string> filelist;

	filelist.push_back("server_ident.key");
	filelist.push_back("server_ident.priv");
	filelist.push_back("server_ident.pub");

	bool has_error=false;
	for(size_t i=0;i<filelist.size();++i)
	{
		if(!copy_file(os_file_prefix(srcfolder + os_file_sep() + filelist[i]), os_file_prefix(bfolder + os_file_sep() + filelist[i]+"~"), true))
		{
			Server->Log("Error backing up "+filelist[i], LL_ERROR);
			has_error=true;
		}
	}

	return !has_error;
}

bool ServerCleanupThread::cleanup_clientlists()
{
	std::string srcfolder = Server->getServerWorkingDir()+os_file_sep()+"urbackup";
	std::vector<SFile> files = getFiles(srcfolder);

	bool has_error=false;
	for(size_t i=0;i<files.size();++i)
	{
		if(next(files[i].name, 0, "clientlist_b_"))
		{
			int backupid = watoi(getbetween("clientlist_b_", ".ub", files[i].name));

			if(cleanupdao->hasMoreRecentFileBackup(backupid).exists)
			{
				if(!Server->deleteFile(os_file_prefix(srcfolder+os_file_sep()+files[i].name)))
				{
					has_error=true;
				}
			}
		}
	}

	return !has_error;
}




#endif //CLIENT_ONLY
