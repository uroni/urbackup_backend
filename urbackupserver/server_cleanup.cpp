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

#ifndef CLIENT_ONLY

#include "server_cleanup.h"
#include "../Interface/Server.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/ThreadPool.h"
#include "database.h"
#include "../stringtools.h"
#include "server_settings.h"
#include "../urbackupcommon/os_functions.h"
#include "server_update_stats.h"
#include "server_update.h"
#include "server_status.h"
#include "server_get.h"
#include "server.h"
#include "snapshot_helper.h"
#include "apps/cleanup_cmd.h"
#include <stdio.h>
#include <algorithm>

IMutex *ServerCleanupThread::mutex=NULL;
ICondition *ServerCleanupThread::cond=NULL;
bool ServerCleanupThread::update_stats=false;
IMutex *ServerCleanupThread::a_mutex=NULL;
bool ServerCleanupThread::update_stats_interruptible=false;
volatile bool ServerCleanupThread::do_quit=false;

const unsigned int min_cleanup_interval=12*60*60;

void ServerCleanupThread::initMutex(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
	a_mutex=Server->createMutex();
}

void ServerCleanupThread::destroyMutex(void)
{
	Server->destroy(mutex);
	Server->destroy(cond);
	Server->destroy(a_mutex);
}

ServerCleanupThread::ServerCleanupThread(CleanupAction cleanup_action)
	: cleanup_action(cleanup_action)
{
}

void ServerCleanupThread::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	if(cleanup_action.action!=ECleanupAction_None)
	{
		createQueries();

		switch(cleanup_action.action)
		{
		case ECleanupAction_DeleteFilebackup:
			deleteFileBackup(cleanup_action.backupfolder, cleanup_action.clientid, cleanup_action.backupid, cleanup_action.force_remove);
			break;
		case ECleanupAction_FreeMinspace:
			{
				bool b = do_cleanup(cleanup_action.minspace, cleanup_action.switch_to_wal);
				if(cleanup_action.result!=NULL)
				{
					*(cleanup_action.result)=b;
				}
			} break;
		case ECleanupAction_RemoveUnknown:
			do_remove();
			break;
		}
		
		destroyQueries();
		delete this;
		return;
	}



	unsigned int last_cleanup=0;

	{
		IScopedLock lock(mutex);
		cond->wait(&lock, 60000);

		if(do_quit)
		{
			delete this;
			return;
		}
	}

	{
		ScopedActiveThread sat;
		ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings");
		if( settings->getValue("autoshutdown", "false")=="true" )
		{
			IScopedLock lock(a_mutex);

			createQueries();
			deletePendingClients();
			do_cleanup();
			destroyQueries();

			backup_database();
		}

		if( settings->getValue("autoupdate_clients", "true")=="true" )
		{
			IScopedLock lock(a_mutex);
			ServerUpdate upd;
			upd();
		}

		Server->destroy(settings);
	}

	while(true)
	{
		{
			IScopedLock lock(mutex);
			if(!update_stats)
			{
				cond->wait(&lock, 3600000);
			}
			if(do_quit)
			{
				return;
			}
			if(update_stats)
			{
				update_stats=false;
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
		}
		db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		IQuery *q=db->Prepare("SELECT strftime('%H','now', 'localtime') AS time", false);
		db_results res=q->Read();
		db->destroyQuery(q);
		if(res.empty())
			Server->Log("Reading time failed!", LL_ERROR);
		else
		{
			int chour=watoi(res[0][L"time"]);
			ServerSettings settings(db);
			std::vector<STimeSpan> tw=settings.getCleanupWindow();
			if( (!tw.empty() && BackupServerGet::isInBackupWindow(tw)) || ( tw.empty() && (chour==3 || chour==4) ) && Server->getTimeSeconds()-last_cleanup>min_cleanup_interval)
			{
				IScopedLock lock(a_mutex);

				ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings");

				ScopedActiveThread sat;

				if( settings->getValue("autoupdate_clients", "true")=="true" )
				{
					ServerUpdate upd;
					upd();
				}

				createQueries();
				deletePendingClients();
				do_cleanup();
				destroyQueries();

				backup_database();

				Server->destroy(settings);

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

void ServerCleanupThread::do_cleanup(void)
{
	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
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

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
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
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
	}
	
	if(minspace>0)
	{
		Server->Log("Space to free: "+PrettyPrintBytes(minspace), LL_INFO);
	}

	removeerr.clear();
	cleanup_images(minspace);
	cleanup_files(minspace);

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
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
		db->Write("PRAGMA shrink_memory");
	}

	return success;
}

void ServerCleanupThread::do_remove(void)
{
	ServerSettings settings(db);

	std::wstring backupfolder=settings.getSettings()->backupfolder;

	db_results res_clients=q_get_clients->Read();
	q_get_clients->Reset();

	for(size_t i=0;i<res_clients.size();++i)
	{
		int clientid=watoi(res_clients[i][L"id"]);
		std::wstring clientname=res_clients[i][L"name"];

		q_get_backups_clientid->Bind(clientid);
		db_results res_file_backups=q_get_backups_clientid->Read();
		q_get_backups_clientid->Reset();

		for(size_t j=0;j<res_file_backups.size();++j)
		{
			std::wstring backup_path=backupfolder+os_file_sep()+clientname+os_file_sep()+res_file_backups[j][L"path"];
			if(!os_directory_exists(backup_path))
			{
				Server->Log(L"Path for file backup [id="+res_file_backups[j][L"id"]+L" path="+res_file_backups[j][L"path"]+L" clientname="+clientname+L"] does not exist. Deleting it from the database.", LL_WARNING);
				q_remove_file_backup->Bind(res_file_backups[j][L"id"]);
				q_remove_file_backup->Write();
				q_remove_file_backup->Reset();
			}
		}

		q_get_imagebackups_clientid->Bind(clientid);
		db_results res_image_backups=q_get_imagebackups_clientid->Read();
		q_get_imagebackups_clientid->Reset();

		for(size_t j=0;j<res_image_backups.size();++j)
		{
			std::wstring backup_path=res_image_backups[j][L"path"];

			IFile *tf=Server->openFile(os_file_prefix(backup_path), MODE_READ);
			if(tf==NULL)
			{
				Server->Log(L"Image backup [id="+res_image_backups[j][L"id"]+L" path="+res_image_backups[j][L"path"]+L" clientname="+clientname+L"] does not exist. Deleting it from the database.", LL_WARNING);
				q_remove_image->Bind(res_image_backups[j][L"id"]);
				q_remove_image->Write();
				q_remove_image->Reset();
			}
			else
			{
				Server->destroy(tf);
			}
		}

		std::vector<SFile> files=getFiles(backupfolder+os_file_sep()+clientname);

		q_get_client_images->Bind(clientid);
		db_results res_images=q_get_client_images->Read();
		q_get_client_images->Reset();

		for(size_t j=0;j<files.size();++j)
		{
			SFile cf=files[j];

			if(cf.name==L"current")
				continue;

			if(cf.isdir)
			{
				q_find_filebackup->Bind(clientid);
				q_find_filebackup->Bind(cf.name);
				db_results res=q_find_filebackup->Read();
				q_find_filebackup->Reset();

				if(res.empty())
				{
					Server->Log(L"File backup \""+cf.name+L"\" of client \""+clientname+L"\" not found in database. Deleting it.", LL_WARNING);
					std::wstring rm_dir=backupfolder+os_file_sep()+clientname+os_file_sep()+cf.name;
					if(!os_remove_nonempty_dir(rm_dir))
					{
						Server->Log(L"Could not delete directory \""+rm_dir+L"\"", LL_ERROR);
					}
				}
			}
			else
			{
				std::wstring extension=findextension(cf.name);

				if(extension!=L"vhd")
					continue;

				bool found=false;

				for(size_t k=0;k<res_images.size();++k)
				{
					if(ExtractFileName(res_images[k][L"path"])==cf.name)
					{
						found=true;
						break;
					}
				}

				if(!found)
				{
					Server->Log(L"Image backup \""+cf.name+L"\" of client \""+clientname+L"\" not found in database. Deleting it.", LL_WARNING);
					std::wstring rm_file=backupfolder+os_file_sep()+clientname+os_file_sep()+cf.name;
					if(!Server->deleteFile(rm_file))
					{
						Server->Log(L"Could not delete file \""+rm_file+L"\"", LL_ERROR);
					}
					if(!Server->deleteFile(rm_file+L".mbr"))
					{
						Server->Log(L"Could not delete file \""+rm_file+L".mbr\"", LL_ERROR);
					}
					if(!Server->deleteFile(rm_file+L".hash"))
					{
						Server->Log(L"Could not delete file \""+rm_file+L".hash\"", LL_ERROR);
					}
				}
			}
		}
	}
}

int ServerCleanupThread::hasEnoughFreeSpace(int64 minspace, ServerSettings *settings)
{
	if(minspace!=-1)
	{
		std::wstring path=settings->getSettings()->backupfolder;
		int64 available_space=os_free_space(os_file_prefix(path));
		if(available_space==-1)
		{
			Server->Log(L"Error getting free space for path \""+path+L"\"", LL_ERROR);
			return -1;
		}
		else
		{
			if(available_space>minspace)
			{
				Server->Log(L"Enough free space now.", LL_DEBUG);
				return 1;
			}
		}
		Server->Log("Free space: "+PrettyPrintBytes(available_space), LL_DEBUG);
	}
	return 0;
}

bool ServerCleanupThread::deleteAndTruncateFile(std::wstring path)
{
	if(!Server->deleteFile(os_file_prefix(path)))
	{
		os_file_truncate(os_file_prefix(path), 0);
		return false;
	}
	return true;
}

bool ServerCleanupThread::deleteImage(std::wstring path)
{
	bool b=true;
	if(!deleteAndTruncateFile(path))
	{
		b=false;
	}
	if(!deleteAndTruncateFile(path+L".hash"))
	{
		b=false;
	}
	if(!deleteAndTruncateFile(path+L".mbr"))
	{
		b=false;
	}
	return b;
}

void ServerCleanupThread::cleanup_images(int64 minspace)
{
	db_results res=q_incomplete_images->Read();
	for(size_t i=0;i<res.size();++i)
	{
		Server->Log(L"Deleting incomplete image file \""+res[i][L"path"]+L"\"...", LL_INFO);
		if(!deleteImage(res[i][L"path"]))
		{
			Server->Log(L"Deleting incomplete image \""+res[i][L"path"]+L"\" failed.", LL_WARNING); 
		}
		q_remove_image->Bind(watoi(res[i][L"id"]));
		q_remove_image->Write();
		q_remove_image->Reset();
	}

	{
		ServerSettings settings(db);
		int r=hasEnoughFreeSpace(minspace, &settings);
		if( r==-1 || r==1)
			return;
	}

	res=q_get_clients_sortimages->Read();
	q_get_clients_sortimages->Reset();
	for(size_t i=0;i<res.size();++i)
	{
		int clientid=watoi(res[i][L"c.id"]);
		ServerSettings settings(db, clientid);

		int max_image_full=settings.getSettings()->max_image_full;
		if(minspace!=-1)
		{
			max_image_full=settings.getSettings()->min_image_full;
		}

		std::vector<int> notit;

		int backupid;
		int full_image_num=(int)getImagesFullNum(clientid, backupid, notit);
		Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(full_image_num)+" full image backups max="+nconvert(max_image_full), LL_DEBUG);
		while(full_image_num>max_image_full)
		{
			q_get_image_info->Bind(backupid);
			db_results res_info=q_get_image_info->Read();
			
			q_get_image_info->Reset();
			q_get_clientname->Bind(clientid);
			db_results res_name=q_get_clientname->Read();
			q_get_clientname->Reset();
			if(!res_name.empty() && !res_info.empty())
			{
				Server->Log(L"Deleting full image backup ( id="+res_info[0][L"id"]+L", backuptime="+res_info[0][L"backuptime"]+L", path="+res_info[0][L"path"]+L", letter="+res_info[0][L"letter"]+L" ) from client \""+res_name[0][L"name"]+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
			}

			if(!findUncompleteImageRef(backupid) )
			{
				std::vector<int> assoc=getAssocImages(backupid);
				int64 corr=0;
				for(size_t i=0;i<assoc.size();++i)
				{
					int64 is=getImageSize(assoc[i]);
					if(is!=-1) corr+=is;
					removeImage(assoc[i], false);
				}
				if(!removeImage(backupid, true, corr))
				{
					notit.push_back(backupid);
				}
				
			}
			else
			{
				Server->Log("Backup image has dependant image which is not complete");
				notit.push_back(backupid);
			}

			int r=hasEnoughFreeSpace(minspace, &settings);
			if( r==-1 || r==1 )
				return;
				
			full_image_num=(int)getImagesFullNum(clientid, backupid, notit);
		}

		notit.clear();

		int max_image_incr=settings.getSettings()->max_image_incr;
		if(minspace!=-1)
			max_image_incr=settings.getSettings()->min_image_incr;

		int incr_image_num=(int)getImagesIncrNum(clientid, backupid, notit);
		Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(incr_image_num)+" incremental image backups max="+nconvert(max_image_incr), LL_DEBUG);
		while(incr_image_num>max_image_incr)
		{
			q_get_image_info->Bind(backupid);
			db_results res_info=q_get_image_info->Read();
			q_get_image_info->Reset();
			q_get_clientname->Bind(clientid);
			db_results res_name=q_get_clientname->Read();
			q_get_clientname->Reset();
			if(!res_name.empty() && !res_info.empty())
			{
				Server->Log(L"Deleting incremental image backup ( id="+res_info[0][L"id"]+L", backuptime="+res_info[0][L"backuptime"]+L", path="+res_info[0][L"path"]+L", letter="+res_info[0][L"letter"]+L" ) from client \""+res_name[0][L"name"]+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
			}

			if(!findUncompleteImageRef(backupid) )
			{
				std::vector<int> assoc=getAssocImages(backupid);
				int64 corr=0;
				for(size_t i=0;i<assoc.size();++i)
				{
					int64 is=getImageSize(assoc[i]);
					if(is!=-1) corr+=is;
					removeImage(assoc[i], false);
				}
				if(!removeImage(backupid, true, corr))
				{
					notit.push_back(backupid);
				}
			}
			else
			{
				Server->Log("Backup image has dependant image which is not complete");
				notit.push_back(backupid);
			}

			int r=hasEnoughFreeSpace(minspace, &settings);
			if( r==-1 || r==1 )
				return;
				
			incr_image_num=(int)getImagesIncrNum(clientid, backupid, notit);
		}
	}
}

bool ServerCleanupThread::removeImage(int backupid, bool update_stat, int64 size_correction, bool force_remove)
{
	bool ret=true;

	ServerStatus::updateActive();

	q_get_image_refs->Bind(backupid);
	db_results res=q_get_image_refs->Read();
	q_get_image_refs->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		bool b=removeImage(watoi(res[i][L"id"]), true, getImageSize(watoi(res[i][L"id"])));
		if(!b)
			ret=false;
	}

	q_get_image_path->Bind(backupid);
	res=q_get_image_path->Read();
	q_get_image_path->Reset();

	if(!res.empty())
	{
		_i64 stat_id;
		if(update_stat)
		{
			q_del_image_stats->Bind(size_correction);
			q_del_image_stats->Bind(backupid);
			q_del_image_stats->Write();
			q_del_image_stats->Reset();
			stat_id=db->getLastInsertID();
		}

		if( deleteImage(res[0][L"path"] ) || force_remove )
		{
			db->BeginTransaction();
			q_remove_image->Bind(backupid);
			q_remove_image->Write();
			q_remove_image->Reset();
			removeImageSize(backupid);
			db->EndTransaction();
		}
		else
		{
			ret=false;
		}

		if(update_stat)
		{
			q_image_stats_stop->Bind(stat_id);
			q_image_stats_stop->Write();
			q_image_stats_stop->Reset();
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
	q_get_image_refs->Bind(backupid);
	db_results res=q_get_image_refs->Read();
	q_get_image_refs->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		if( watoi(res[i][L"complete"])!=1 || findUncompleteImageRef(watoi(res[i][L"id"])) )
			return true;
	}
	return false;
}

size_t ServerCleanupThread::getImagesFullNum(int clientid, int &backupid_top, const std::vector<int> &notit)
{
	q_get_full_num_images->Bind(clientid);
	db_results res=q_get_full_num_images->Read();
	q_get_full_num_images->Reset();
	std::map<std::wstring, std::vector<int> > images_ids;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring letter=res[i][L"letter"];
		int cid=watoi(res[i][L"id"]);

		if(std::find(notit.begin(), notit.end(), cid)==notit.end())
		{
			images_ids[letter].push_back(cid);			
		}
	}

	size_t max_nimages=0;
	for(std::map<std::wstring, std::vector<int> >::iterator iter=images_ids.begin();iter!=images_ids.end();++iter)
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
	q_get_incr_num_images->Bind(clientid);
	db_results res=q_get_incr_num_images->Read();
	q_get_incr_num_images->Reset();
	std::map<std::wstring, std::vector<int> > images_ids;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring letter=res[i][L"letter"];
		int cid=watoi(res[i][L"id"]);

		if(std::find(notit.begin(), notit.end(), cid)==notit.end())
		{
			images_ids[letter].push_back(cid);			
		}
	}

	size_t max_nimages=0;
	for(std::map<std::wstring, std::vector<int> >::iterator iter=images_ids.begin();iter!=images_ids.end();++iter)
	{
		if(iter->second.size()>max_nimages)
		{
			backupid_top=iter->second[0];
			max_nimages=iter->second.size();
		}
	}
	return max_nimages;
}

void ServerCleanupThread::cleanup_files(int64 minspace)
{
	bool deleted_something=true;
	while(deleted_something)
	{
		deleted_something=false;
		{
			ServerSettings settings(db);
			int r=hasEnoughFreeSpace(minspace, &settings);
			if( r==-1 || r==1 )
					return;
		}

		db_results res=q_get_clients_sortfiles->Read();
		q_get_clients_sortfiles->Reset();
		for(size_t i=0;i<res.size();++i)
		{
			bool deleted_something_client=false;

			int clientid=watoi(res[i][L"id"]);
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
			Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(full_file_num)+" full file backups max="+nconvert(max_file_full), LL_DEBUG);
			while(full_file_num>max_file_full )
			{
				q_get_filebackup_info->Bind(backupid);
				db_results res_info=q_get_filebackup_info->Read();
				q_get_filebackup_info->Reset();
				q_get_clientname->Bind(clientid);
				db_results res_name=q_get_clientname->Read();
				q_get_clientname->Reset();
				if(!res_name.empty() && !res_info.empty())
				{
					Server->Log(L"Deleting full file backup ( id="+res_info[0][L"id"]+L", backuptime="+res_info[0][L"backuptime"]+L", path="+res_info[0][L"path"]+L" ) from client \""+res_name[0][L"name"]+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
				}
				bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid);
				
				Server->Log("Done.", LL_INFO);

				int r=hasEnoughFreeSpace(minspace, &settings);
				if( r==-1 || r==1 )
					return;

				if(b)
				{
					deleted_something_client=true;
        			deleted_something=true;
        			break;
        		}
        			
        		full_file_num=(int)getFilesFullNum(clientid, backupid);
			}

			if(deleted_something_client==true)
				continue;

			int incr_file_num=(int)getFilesIncrNum(clientid, backupid);
			Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(incr_file_num)+" icremental file backups max="+nconvert(max_file_incr), LL_DEBUG);
			while(incr_file_num>max_file_incr )
			{
				q_get_filebackup_info->Bind(backupid);
				db_results res_info=q_get_filebackup_info->Read();
				q_get_filebackup_info->Reset();
				q_get_clientname->Bind(clientid);
				db_results res_name=q_get_clientname->Read();
				q_get_clientname->Reset();
				if(!res_name.empty() && !res_info.empty())
				{
					Server->Log(L"Deleting incremental file backup ( id="+res_info[0][L"id"]+L", backuptime="+res_info[0][L"backuptime"]+L", path="+res_info[0][L"path"]+L" ) from client \""+res_name[0][L"name"]+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
				}
				bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid);
				
				Server->Log("Done.", LL_INFO);

				int r=hasEnoughFreeSpace(minspace, &settings);
				if( r==-1 || r==1 )
					return;

				if(b)
				{
					deleted_something_client=true;
					deleted_something=true;
					break;
				}
				incr_file_num=(int)getFilesIncrNum(clientid, backupid);
			}

			if(deleted_something_client==true)
				continue;
		}
	}

}

size_t ServerCleanupThread::getFilesFullNum(int clientid, int &backupid_top)
{
	q_get_full_num_files->Bind(clientid);
	db_results res=q_get_full_num_files->Read();
	q_get_full_num_files->Reset();
	db_results no_err_res;
	if(!removeerr.empty())
	{
		for(size_t i=0;i<res.size();++i)
		{
			int bid=watoi(res[i][L"id"]);
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
		backupid_top=watoi(no_err_res[0][L"id"]);
	}
	return no_err_res.size();
}

size_t ServerCleanupThread::getFilesIncrNum(int clientid, int &backupid_top)
{
	q_get_incr_num_files->Bind(clientid);
	db_results res=q_get_incr_num_files->Read();
	q_get_incr_num_files->Reset();
	db_results no_err_res;
	if(!removeerr.empty())
	{
		for(size_t i=0;i<res.size();++i)
		{
			int bid=watoi(res[i][L"id"]);
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
		backupid_top=watoi(no_err_res[0][L"id"]);
	}
	return no_err_res.size();
}

bool ServerCleanupThread::deleteFileBackup(const std::wstring &backupfolder, int clientid, int backupid, bool force_remove)
{
	ServerStatus::updateActive();

	std::wstring clientname;
	q_get_clientname->Bind(clientid);
	db_results res=q_get_clientname->Read();
	q_get_clientname->Reset();
	if(res.empty())
	{
		Server->Log("Error getting clientname in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	clientname=res[0][L"name"];

	std::wstring backuppath;
	q_get_backuppath->Bind(backupid);
	res=q_get_backuppath->Read();
	q_get_backuppath->Reset();

	if(res.empty())
	{
		Server->Log("Error getting backuppath in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	backuppath=res[0][L"path"];

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

	std::wstring path=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath;
	bool b=false;
	if( BackupServer::isSnapshotsEnabled())
	{
		b=SnapshotHelper::removeFilesystem(clientname, backuppath);

		if(!b)
		{
			b=os_remove_nonempty_dir(os_file_prefix(path));

			if(!b && SnapshotHelper::isSubvolume(clientname, backuppath) )
			{
				Server->Log("Deleting directory failed. Trying to truncate all files in subvolume to zero...", LL_ERROR);
				b=truncate_files_recurisve(os_file_prefix(path));

				if(b)
				{
					b=os_remove_nonempty_dir(os_file_prefix(path));
				}
			}
		}
	}
	else
	{
		b=os_remove_nonempty_dir(os_file_prefix(path));
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
			Server->Log(L"Warning: Directory doesn't exist: \""+path+L"\"", LL_WARNING);
		}
		else
		{
			del=false;
			removeerr.push_back(backupid);
			Server->Log(L"Error removing directory \""+path+L"\"", LL_ERROR);
			err=true;
		}
	}
	if(os_directory_exists(os_file_prefix(path)) )
	{
		del=false;
		Server->Log(L"Directory still exists. Deleting backup failed. Path: \""+path+L"\"", LL_ERROR);
		err=true;
		removeerr.push_back(backupid);
	}
	if(del || force_remove)
	{
		db->DetachDBs();
		db->BeginTransaction();
		q_move_files->Bind(backupid);
		q_move_files->Write();
		q_move_files->Reset();
		q_delete_files->Bind(backupid);
		q_delete_files->Write();
		q_delete_files->Reset();
		q_remove_file_backup->Bind(backupid);
		q_remove_file_backup->Write();
		q_remove_file_backup->Reset();
		db->EndTransaction();
		db->AttachDBs();
	}

	ServerStatus::updateActive();
	
	return !err;
}

void ServerCleanupThread::removeImageSize(int backupid)
{
	q_remove_image_size->Bind(backupid);
	q_remove_image_size->Bind(backupid);
	q_remove_image_size->Bind(backupid);
	q_remove_image_size->Write();
	q_remove_image_size->Reset();
}

void ServerCleanupThread::removeClient(int clientid)
{
	std::wstring clientname;
	q_get_clientname->Bind(clientid);
	db_results res=q_get_clientname->Read();
	q_get_clientname->Reset();
	if(!res.empty())
		clientname=res[0][L"name"];

	Server->Log(L"Deleting client with id \""+convert(clientid)+L"\" name \""+clientname+L"\"", LL_INFO);
	//remove image backups
	do
	{
		q_get_client_images->Bind(clientid);
		res=q_get_client_images->Read();
		q_get_client_images->Reset();

		if(!res.empty())
		{
			int backupid=watoi(res[0][L"id"]);
			Server->Log("Removing image with id \""+nconvert(backupid)+"\"", LL_INFO);
			removeImage(backupid, true, 0, true);
		}
	}while(!res.empty());

	//remove file backups
	ServerSettings settings(db);
	do
	{
		q_get_client_filebackups->Bind(clientid);
		res=q_get_client_filebackups->Read();
		q_get_client_filebackups->Reset();

		if(!res.empty())
		{
			int backupid=watoi(res[0][L"id"]);
			Server->Log("Removing file backup with id \""+nconvert(backupid)+"\"", LL_INFO);
			bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid, true);
			if(b)
				Server->Log("Removing file backup with id \""+nconvert(backupid)+"\" successfull.", LL_INFO);
			else
				Server->Log("Removing file backup with id \""+nconvert(backupid)+"\" failed.", LL_ERROR);
		}
	}while(!res.empty());

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
	Server->deleteFile(settings.getSettings()->backupfolder+os_file_sep()+L"clients"+os_file_sep()+clientname);
}

void ServerCleanupThread::deletePendingClients(void)
{
	db_results res=db->Read("SELECT id, name FROM clients WHERE delete_pending=1");
	for(size_t i=0;i<res.size();++i)
	{
		SStatus status=ServerStatus::getStatus(res[i][L"name"]);
		if(status.done==false && status.has_status==true)
		{
			Server->Log(L"Cannot remove client \""+res[i][L"name"]+L"\" ( with id "+res[i][L"id"]+L"): Client is online or backup is in progress", LL_WARNING);
			continue;
		}

		removeClient(watoi(res[i][L"id"]));
	}
}

std::vector<int> ServerCleanupThread::getAssocImages(int backupid)
{
	q_get_assoc_img->Bind(backupid);
	db_results res=q_get_assoc_img->Read();
	q_get_assoc_img->Reset();
	std::vector<int> ret;
	for(size_t i=0;i<res.size();++i)
	{
		ret.push_back(watoi(res[i][L"assoc_id"]));
	}
	return ret;
}

int64 ServerCleanupThread::getImageSize(int backupid)
{
	q_get_image_size->Bind(backupid);
	db_results res=q_get_image_size->Read();
	q_get_image_size->Reset();
	if(!res.empty())
	{
		return os_atoi64(wnarrow(res[0][L"size_bytes"]));
	}
	return -1;
}

void ServerCleanupThread::createQueries(IDatabase *pDb)
{
	if(pDb!=NULL)
	{
		db=pDb;
	}
	q_incomplete_images=db->Prepare("SELECT id, path FROM backup_images WHERE complete=0 AND running<datetime('now','-300 seconds')", false);
	q_remove_image=db->Prepare("DELETE FROM backup_images WHERE id=?", false);
	q_get_clients_sortfiles=db->Prepare("SELECT DISTINCT c.id FROM clients c INNER JOIN backups b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_get_clients_sortimages=db->Prepare("SELECT DISTINCT c.id FROM clients c INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_get_full_num_images=db->Prepare("SELECT id,letter FROM backup_images WHERE clientid=? AND incremental=0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	q_get_image_refs=db->Prepare("SELECT id,complete FROM backup_images WHERE incremental<>0 AND incremental_ref=?", false);
	q_get_image_path=db->Prepare("SELECT path FROM backup_images WHERE id=?", false);
	q_get_incr_num_images=db->Prepare("SELECT id,letter FROM backup_images WHERE clientid=? AND incremental<>0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	q_get_full_num_files=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	q_get_incr_num_files=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	q_get_clientname=db->Prepare("SELECT name FROM clients WHERE id=?", false);
	q_get_backuppath=db->Prepare("SELECT path FROM backups WHERE id=?", false);
	q_delete_files=db->Prepare("DELETE FROM files WHERE backupid=?", false);
	q_remove_file_backup=db->Prepare("DELETE FROM backups WHERE id=?", false);
	q_get_filebackup_info=db->Prepare("SELECT id, backuptime, path FROM backups WHERE id=?", false);
	q_get_image_info=db->Prepare("SELECT id, backuptime, path, letter FROM backup_images WHERE id=?", false);
	q_move_files=db->Prepare("INSERT INTO files_del (backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del) SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del FROM files WHERE backupid=?", false);
	q_remove_image_size=db->Prepare("UPDATE clients SET bytes_used_images=(SELECT bytes_used_images FROM clients WHERE id=(SELECT clientid FROM backup_images WHERE id=?))-(SELECT size_bytes FROM backup_images WHERE id=?) WHERE id=(SELECT clientid FROM backup_images WHERE id=?)",false);
	q_del_image_stats=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental) SELECT id, 1 AS image, (size_bytes+?) AS delsize, clientid, incremental FROM backup_images WHERE id=?", false);
	q_image_stats_stop=db->Prepare("UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=?", false);
	q_get_client_images=db->Prepare("SELECT id, path FROM backup_images WHERE clientid=?", false);
	q_get_client_filebackups=db->Prepare("SELECT id FROM backups WHERE clientid=?", false);
	q_get_assoc_img=db->Prepare("SELECT assoc_id FROM assoc_images WHERE img_id=?", false);
	q_get_image_size=db->Prepare("SELECT size_bytes FROM backup_images WHERE id=?", false);
	q_get_clients=db->Prepare("SELECT id, name FROM clients", false);
	q_get_backups_clientid=db->Prepare("SELECT id, backuptime, path FROM backups WHERE clientid=?", false);
	q_get_imagebackups_clientid=db->Prepare("SELECT id, letter, path FROM backup_images WHERE clientid=?", false);
	q_find_filebackup=db->Prepare("SELECT id FROM backups WHERE clientid=? AND path=?", false);
}

void ServerCleanupThread::destroyQueries(void)
{
	db->destroyQuery(q_incomplete_images);
	db->destroyQuery(q_remove_image);
	db->destroyQuery(q_get_clients_sortfiles);
	db->destroyQuery(q_get_clients_sortimages);
	db->destroyQuery(q_get_full_num_images);
	db->destroyQuery(q_get_image_refs);
	db->destroyQuery(q_get_image_path);
	db->destroyQuery(q_get_incr_num_images);
	db->destroyQuery(q_get_full_num_files);
	db->destroyQuery(q_get_incr_num_files);
	db->destroyQuery(q_get_clientname);
	db->destroyQuery(q_get_backuppath);
	db->destroyQuery(q_delete_files);
	db->destroyQuery(q_remove_file_backup);
	db->destroyQuery(q_get_filebackup_info);
	db->destroyQuery(q_get_image_info);
	db->destroyQuery(q_move_files);
	db->destroyQuery(q_remove_image_size);
	db->destroyQuery(q_del_image_stats);
	db->destroyQuery(q_image_stats_stop);
	db->destroyQuery(q_get_client_images);
	db->destroyQuery(q_get_client_filebackups);
	db->destroyQuery(q_get_assoc_img);
	db->destroyQuery(q_get_image_size);
	db->destroyQuery(q_get_clients);
	db->destroyQuery(q_get_backups_clientid);
	db->destroyQuery(q_get_imagebackups_clientid);
	db->destroyQuery(q_find_filebackup);
}

void ServerCleanupThread::backup_database(void)
{
	ServerSettings settings(db);

	if(settings.getSettings()->backup_database)
	{
		std::wstring bfolder=settings.getSettings()->backupfolder+os_file_sep()+L"urbackup";
		if(!os_directory_exists(bfolder) )
		{
			os_create_dir(bfolder);
		}
		else
		{
			rename(Server->ConvertToUTF8(bfolder+os_file_sep()+L"backup_server.db").c_str(), Server->ConvertToUTF8(bfolder+os_file_sep()+L"backup_server.db~").c_str() );
			rename(Server->ConvertToUTF8(bfolder+os_file_sep()+L"backup_server_settings.db").c_str(), Server->ConvertToUTF8(bfolder+os_file_sep()+L"backup_server_settings.db~").c_str() );
		}

		Server->Log("Starting database backup...", LL_INFO);
		bool b=db->Backup(Server->ConvertToUTF8(bfolder+os_file_sep()+L"backup_server.db"));
		Server->Log("Database backup done.", LL_INFO);
		if(!b)
		{
			Server->Log("Backing up database failed", LL_ERROR);
		}
		else
		{
			Server->deleteFile(bfolder+os_file_sep()+L"backup_server.db~");
			Server->deleteFile(bfolder+os_file_sep()+L"backup_server_settings.db~");
		}
	}
}

void ServerCleanupThread::doQuit(void)
{
	do_quit=true;
	cond->notify_all();
}

bool ServerCleanupThread::truncate_files_recurisve(std::wstring path)
{
	std::vector<SFile> files=getFiles(path);

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
				Server->Log(L"Truncating file \""+path+os_file_sep()+files[i].name+L"\" failed. Stopping truncating.", LL_ERROR);
				return false;
			}
		}
	}
	return true;
}

bool ServerCleanupThread::cleanupSpace(int64 minspace, bool switch_to_wal)
{
	bool result;
	Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(minspace, &result, switch_to_wal)));
	return result;
}

void ServerCleanupThread::removeUnknown(void)
{
	Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(ECleanupAction_RemoveUnknown)));
}

#endif //CLIENT_ONLY
