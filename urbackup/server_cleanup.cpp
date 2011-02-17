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
#include "os_functions.h"
#include "server_update_stats.h"
#include "server_update.h"
#include "server_status.h"

IMutex *ServerCleanupThread::mutex=NULL;
ICondition *ServerCleanupThread::cond=NULL;
bool ServerCleanupThread::update_stats=false;
IMutex *ServerCleanupThread::a_mutex=NULL;

void ServerCleanupThread::initMutex(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
	a_mutex=Server->createMutex();
}

void ServerCleanupThread::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	{
		ScopedActiveThread sat;
		ISettingsReader *settings=Server->createDBSettingsReader(db, "settings");
		if( settings->getValue("autoshutdown", "false")=="true" )
		{
			IScopedLock lock(a_mutex);
			createQueries();
			do_cleanup();
			destroyQueries();
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
			if(update_stats)
			{
				update_stats=false;
				lock.relock(NULL);
				Server->Log("Updating statistics...", LL_INFO);

				ScopedActiveThread sat;

				{
					IScopedLock lock(a_mutex);
					ServerUpdateStats sus;
					sus();					
				}

				Server->Log("Done updating statistics.", LL_INFO);
			}
		}
		db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		IQuery *q=db->Prepare("SELECT strftime('%H','now') AS time", false);
		db_results res=q->Read();
		db->destroyQuery(q);
		if(res.empty())
			Server->Log("Reading time failed!", LL_ERROR);
		else
		{
			int chour=watoi(res[0][L"time"]);
			if( chour==3 || chour==4 )
			{
				IScopedLock lock(a_mutex);

				ISettingsReader *settings=Server->createDBSettingsReader(db, "settings");

				ScopedActiveThread sat;

				if( settings->getValue("autoupdate_clients", "true")=="true" )
				{
					ServerUpdate upd;
					upd();
				}

				createQueries();
				do_cleanup();
				destroyQueries();

				Server->destroy(settings);
			}
		}
	}
}

void ServerCleanupThread::updateStats(void)
{
	IScopedLock lock(mutex);
	update_stats=true;
	cond->notify_all();
}

void ServerCleanupThread::do_cleanup(void)
{
	db->Write("PRAGMA cache_size = 10000");

	removeerr.clear();
	cleanup_images();
	cleanup_files();

	Server->Log("Updating statistics...", LL_INFO);
	ServerUpdateStats sus;
	sus();
	Server->Log("Done updating statistics.", LL_INFO);

	db->Write("PRAGMA cache_size = 10");
}

bool ServerCleanupThread::do_cleanup(int64 minspace)
{
	IScopedLock lock(a_mutex);

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	createQueries();

	removeerr.clear();
	cleanup_images(minspace);
	cleanup_files(minspace);

	Server->Log("Updating statistics...", LL_INFO);
	ServerUpdateStats sus;
	sus();
	Server->Log("Done updating statistics.", LL_INFO);

	db->destroyAllQueries();

	ServerSettings settings(db);
	int r=hasEnoughFreeSpace(minspace, &settings);
	return r==1;
}

int ServerCleanupThread::hasEnoughFreeSpace(int64 minspace, ServerSettings *settings)
{
	if(minspace!=-1)
	{
		std::wstring path=settings->getSettings()->backupfolder;
		int64 available_space=os_free_space(path);
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
	}
	return 0;
}

void ServerCleanupThread::cleanup_images(int64 minspace)
{
	db_results res=q_incomplete_images->Read();
	for(size_t i=0;i<res.size();++i)
	{
		Server->Log(L"Deleting incomplete image file \""+res[i][L"path"]+L"\"...", LL_INFO);
		Server->deleteFile(res[i][L"path"]);
		Server->deleteFile(res[i][L"path"]+L".hash");
		Server->deleteFile(res[i][L"path"]+L".mbr");
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
		int clientid=watoi(res[i][L"id"]);
		ServerSettings settings(db, clientid);

		int max_image_full=settings.getSettings()->max_image_full;
		if(minspace!=-1)
		{
			max_image_full=settings.getSettings()->min_image_full;
		}

		std::vector<int> notit;

		int backupid;
		while((int)getImagesFullNum(clientid, backupid, notit)>max_image_full)
		{
			q_get_image_info->Bind(backupid);
			db_results res_info=q_get_image_info->Read();
			q_get_image_info->Reset();
			q_get_clientname->Bind(clientid);
			db_results res_name=q_get_clientname->Read();
			q_get_clientname->Reset();
			if(!res_name.empty() && !res_info.empty())
			{
				Server->Log(L"Deleting full image backup ( id="+res_info[0][L"id"]+L", backuptime="+res_info[0][L"backuptime"]+L", path="+res_info[0][L"path"]+L" ) from client \""+res_name[0][L"name"]+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
			}

			if(!findUncompleteImageRef(backupid) )
			{
				removeImage(backupid);
			}
			else
			{
				Server->Log("Backup image has dependant image which is not complete");
				notit.push_back(backupid);
			}

			int r=hasEnoughFreeSpace(minspace, &settings);
			if( r==-1 || r==1 )
				return;
		}

		notit.clear();

		int max_image_incr=settings.getSettings()->max_image_incr;
		if(minspace!=-1)
			max_image_incr=settings.getSettings()->min_image_incr;

		while((int)getImagesIncrNum(clientid, backupid, notit)>max_image_incr)
		{
			q_get_image_info->Bind(backupid);
			db_results res_info=q_get_image_info->Read();
			q_get_image_info->Reset();
			q_get_clientname->Bind(clientid);
			db_results res_name=q_get_clientname->Read();
			q_get_clientname->Reset();
			if(!res_name.empty() && !res_info.empty())
			{
				Server->Log(L"Deleting incremental image backup ( id="+res_info[0][L"id"]+L", backuptime="+res_info[0][L"backuptime"]+L", path="+res_info[0][L"path"]+L" ) from client \""+res_name[0][L"name"]+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
			}

			if(!findUncompleteImageRef(backupid) )
			{
				removeImage(backupid);
			}
			else
			{
				Server->Log("Backup image has dependant image which is not complete");
				notit.push_back(backupid);
			}

			int r=hasEnoughFreeSpace(minspace, &settings);
			if( r==-1 || r==1 )
				return;
		}
	}
}

void ServerCleanupThread::removeImage(int backupid)
{
	q_get_image_refs->Bind(backupid);
	db_results res=q_get_image_refs->Read();
	q_get_image_refs->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		removeImage(watoi(res[i][L"id"]));
	}

	q_get_image_path->Bind(backupid);
	res=q_get_image_path->Read();
	q_get_image_path->Reset();

	if(!res.empty())
	{
		q_del_image_stats->Bind(backupid);
		q_del_image_stats->Write();
		q_del_image_stats->Reset();
		_i64 stat_id=db->getLastInsertID();

		Server->deleteFile(res[0][L"path"]);
		Server->deleteFile(res[0][L"path"]+L".hash");
		Server->deleteFile(res[0][L"path"]+L".mbr");

		db->BeginTransaction();
		q_remove_image->Bind(backupid);
		q_remove_image->Write();
		q_remove_image->Reset();
		removeImageSize(backupid);
		db->EndTransaction();

		q_image_stats_stop->Bind(stat_id);
		q_image_stats_stop->Write();
		q_image_stats_stop->Reset();
	}
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
	size_t nimages=res.size();
	for(size_t i=0;i<res.size();++i)
	{
		backupid_top=watoi(res[i][L"id"]);
		bool found=false;
		for(size_t j=0;j<notit.size();++j)
		{
			if(notit[j]==backupid_top)
			{
				found=true;
				break;
			}
		}
		if(found)
		{
			backupid_top=-1;
			if(nimages>0)--nimages;
		}
		else
		{
			return nimages;
		}
	}
	return nimages;
}

size_t ServerCleanupThread::getImagesIncrNum(int clientid, int &backupid_top, const std::vector<int> &notit)
{
	q_get_incr_num_images->Bind(clientid);
	db_results res=q_get_incr_num_images->Read();
	q_get_incr_num_images->Reset();
	size_t nimages=res.size();
	for(size_t i=0;i<res.size();++i)
	{
		backupid_top=watoi(res[i][L"id"]);
		bool found=false;
		for(size_t j=0;j<notit.size();++j)
		{
			if(notit[j]==backupid_top)
			{
				found=true;
				break;
			}
		}
		if(found)
		{
			backupid_top=-1;
			if(nimages>0)--nimages;
		}
		else
		{
			return nimages;
		}
	}
	return nimages;
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
			if((int)getFilesFullNum(clientid, backupid)>max_file_full )
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
				deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid);

				int r=hasEnoughFreeSpace(minspace, &settings);
				if( r==-1 || r==1 )
					return;

				deleted_something_client=true;
				deleted_something=true;
			}

			if(deleted_something_client==true)
				continue;

			if((int)getFilesIncrNum(clientid, backupid)>max_file_incr )
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
				deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid);

				int r=hasEnoughFreeSpace(minspace, &settings);
				if( r==-1 || r==1 )
					return;

				deleted_something_client=true;
				deleted_something=true;
				break;
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
	if(!removeerr.empty())
	{
		bool c=true;
		while(c)
		{
			c=false;
			for(size_t i=0;i<res.size();++i)
    		{
    			bool found=false;
    			int bid=watoi(res[i][L"id"]);
				for(size_t j=0;j<removeerr.size();++j)
				{
					if(bid==removeerr[j])
					{
						found=true;
						break;
					}
				}
				if(found)
				{
					c=true;
					res.erase(res.begin()+i);
					break;
				}
			}
		}
	}
	if(!res.empty())
	{
		backupid_top=watoi(res[0][L"id"]);
	}
	return res.size();
}

size_t ServerCleanupThread::getFilesIncrNum(int clientid, int &backupid_top)
{
	q_get_incr_num_files->Bind(clientid);
	db_results res=q_get_incr_num_files->Read();
	q_get_incr_num_files->Reset();
	if(!removeerr.empty())
	{
		bool c=true;
		while(c)
		{
			c=false;
			for(size_t i=0;i<res.size();++i)
    			{
    				bool found=false;
    				int bid=watoi(res[i][L"id"]);
				for(size_t j=0;j<removeerr.size();++j)
				{
					if(bid==removeerr[j])
					{
						found=true;
						break;
					}
				}
				if(found)
				{
					c=true;
					res.erase(res.begin()+i);
					break;
				}
			}
		}
	}
	if(!res.empty())
	{
		backupid_top=watoi(res[0][L"id"]);
	}
	return res.size();
}

void ServerCleanupThread::deleteFileBackup(const std::wstring &backupfolder, int clientid, int backupid)
{
	std::wstring clientname;
	q_get_clientname->Bind(clientid);
	db_results res=q_get_clientname->Read();
	q_get_clientname->Reset();
	if(res.empty())
	{
		Server->Log("Error getting clientname in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return;
	}

	clientname=res[0][L"name"];

	std::wstring backuppath;
	q_get_backuppath->Bind(backupid);
	res=q_get_backuppath->Read();
	q_get_backuppath->Reset();

	if(res.empty())
	{
		Server->Log("Error getting backuppath in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return;
	}

	backuppath=res[0][L"path"];

	if(backuppath.empty())
	{
		Server->Log("Error backuppath empty in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return;
	}

	if(backupfolder.empty())
	{
		Server->Log("Error backupfolder empty in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return;
	}

	if(clientname.empty())
	{
		Server->Log("Error clientname empty in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return;
	}

	std::wstring path=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath;
	bool b=os_remove_nonempty_dir(path);
	bool del=true;
	if(!b)
	{
		if(!os_directory_exists(path))
		{
			del=true;
			Server->Log(L"Warning: Directory doesn't exist: \""+path+L"\"", LL_WARNING);
		}
		else
		{
			del=false;
			removeerr.push_back(backupid);
			Server->Log(L"Error removing directory \""+path+L"\"", LL_ERROR);
		}
	}
	if(os_directory_exists(path) )
	{
		del=false;
		Server->Log(L"Directory still exists. Deleting backup failed. Path: \""+path+L"\"", LL_ERROR);
	}
	if(del)
	{
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
	}
}

void ServerCleanupThread::removeImageSize(int backupid)
{
	q_remove_image_size->Bind(backupid);
	q_remove_image_size->Bind(backupid);
	q_remove_image_size->Bind(backupid);
	q_remove_image_size->Write();
	q_remove_image_size->Reset();
}

void ServerCleanupThread::createQueries(void)
{
	q_incomplete_images=db->Prepare("SELECT id, path FROM backup_images WHERE complete=0 AND running<datetime('now','-300 seconds')", false);
	q_remove_image=db->Prepare("DELETE FROM backup_images WHERE id=?", false);
	q_get_clients_sortfiles=db->Prepare("SELECT DISTINCT c.id FROM clients c INNER JOIN backups b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_get_clients_sortimages=db->Prepare("SELECT DISTINCT c.id FROM clients c INNER JOIN backup_images b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_get_full_num_images=db->Prepare("SELECT id FROM backup_images WHERE clientid=? AND incremental=0 AND complete=1 ORDER BY backuptime ASC", false);
	q_get_image_refs=db->Prepare("SELECT id,complete FROM backup_images WHERE incremental<>0 AND incremental_ref=?", false);
	q_get_image_path=db->Prepare("SELECT path FROM backup_images WHERE id=?", false);
	q_get_incr_num_images=db->Prepare("SELECT id FROM backup_images WHERE clientid=? AND incremental<>0 AND complete=1 ORDER BY backuptime ASC", false);
	q_get_full_num_files=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental=0 AND running<datetime('now','-300 seconds') ORDER BY backuptime ASC", false);
	q_get_incr_num_files=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental<>0 AND running<datetime('now','-300 seconds') ORDER BY backuptime ASC", false);
	q_get_clientname=db->Prepare("SELECT name FROM clients WHERE id=?", false);
	q_get_backuppath=db->Prepare("SELECT path FROM backups WHERE id=?", false);
	q_delete_files=db->Prepare("DELETE FROM files WHERE backupid=?", false);
	q_remove_file_backup=db->Prepare("DELETE FROM backups WHERE id=?", false);
	q_get_filebackup_info=db->Prepare("SELECT id, backuptime, path FROM backups WHERE id=?", false);
	q_get_image_info=db->Prepare("SELECT id, backuptime, path FROM backup_images WHERE id=?", false);
	q_move_files=db->Prepare("INSERT INTO files_del (backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental) SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental FROM (files INNER JOIN backups ON files.backupid=backups.id) WHERE backupid=?", false);
	q_remove_image_size=db->Prepare("UPDATE clients SET bytes_used_images=(SELECT bytes_used_images FROM clients WHERE id=(SELECT clientid FROM backup_images WHERE id=?))-(SELECT size_bytes FROM backup_images WHERE id=?) WHERE id=(SELECT clientid FROM backup_images WHERE id=?)",false);
	q_del_image_stats=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental) SELECT id, 1 AS image, size_bytes AS delsize, clientid, incremental FROM backup_images WHERE id=?", false);
	q_image_stats_stop=db->Prepare("UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=?", false);
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
}

#endif //CLIENT_ONLY