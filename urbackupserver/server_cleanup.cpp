/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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
#include "dao/ServerCleanupDAO.h"
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
	: cleanup_action(cleanup_action), cleanupdao(NULL)
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
		cleanupdao=new ServerCleanupDAO(db);

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
		
		delete cleanupdao;
		Server->destroyDatabases(Server->getThreadID());
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

			cleanupdao=new ServerCleanupDAO(db);

			deletePendingClients();
			do_cleanup();
			
			delete cleanupdao;
			cleanupdao=NULL;

			backup_database();
		}

		if( settings->getValue("download_client", "true")=="true" )
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
		db_results res=db->Read("SELECT strftime('%H','now', 'localtime') AS time");
		if(res.empty())
			Server->Log("Reading time failed!", LL_ERROR);
		else
		{
			int chour=watoi(res[0][L"time"]);
			ServerSettings settings(db);
			std::vector<STimeSpan> tw=settings.getCleanupWindow();
			if( ( (!tw.empty() && BackupServerGet::isInBackupWindow(tw)) || ( tw.empty() && (chour==3 || chour==4) ) )
				&& Server->getTimeSeconds()-last_cleanup>min_cleanup_interval)
			{
				IScopedLock lock(a_mutex);

				ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings");

				ScopedActiveThread sat;

				if( settings->getValue("autoupdate_clients", "true")=="true" )
				{
					ServerUpdate upd;
					upd();
				}

				cleanupdao=new ServerCleanupDAO(db);

				deletePendingClients();
				do_cleanup();

				enforce_quotas();

				delete cleanupdao; cleanupdao=NULL;

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

	std::vector<ServerCleanupDAO::SClientInfo> res_clients=cleanupdao->getClients();

	for(size_t i=0;i<res_clients.size();++i)
	{
		int clientid=res_clients[i].id;
		const std::wstring& clientname=res_clients[i].name;

		std::vector<ServerCleanupDAO::SFileBackupInfo> res_file_backups=cleanupdao->getFileBackupsOfClient(clientid);

		for(size_t j=0;j<res_file_backups.size();++j)
		{
			std::wstring backup_path=backupfolder+os_file_sep()+clientname+os_file_sep()+res_file_backups[j].path;
			int backupid=res_file_backups[j].id;
			if(!os_directory_exists(backup_path))
			{
				Server->Log(L"Path for file backup [id="+convert(res_file_backups[j].id)+L" path="+res_file_backups[j].path+L" clientname="+clientname+L"] does not exist. Deleting it from the database.", LL_WARNING);

				db->DetachDBs();
				db->BeginTransaction();
				cleanupdao->moveFiles(backupid);
				cleanupdao->deleteFiles(backupid);
				cleanupdao->removeFileBackup(backupid);
				db->EndTransaction();
				db->AttachDBs();
			}
		}

		std::vector<ServerCleanupDAO::SImageBackupInfo> res_image_backups=cleanupdao->getImageBackupsOfClient(clientid);

		for(size_t j=0;j<res_image_backups.size();++j)
		{
			std::wstring backup_path=res_image_backups[j].path;

			IFile *tf=Server->openFile(os_file_prefix(backup_path), MODE_READ);
			if(tf==NULL)
			{
				Server->Log(L"Image backup [id="+convert(res_image_backups[j].id)+L" path="+res_image_backups[j].path+L" clientname="+clientname+L"] does not exist. Deleting it from the database.", LL_WARNING);
				cleanupdao->removeImage(res_image_backups[j].id);
			}
			else
			{
				Server->destroy(tf);
			}
		}

		std::vector<SFile> files=getFiles(backupfolder+os_file_sep()+clientname);

		std::vector<ServerCleanupDAO::SImageBackupInfo> res_images=cleanupdao->getClientImages(clientid);

		for(size_t j=0;j<files.size();++j)
		{
			SFile cf=files[j];

			if(cf.name==L"current")
				continue;

			if(cf.isdir)
			{
				ServerCleanupDAO::CondInt res_id=cleanupdao->findFileBackup(clientid, cf.name);

				if(!res_id.exists)
				{
					Server->Log(L"File backup \""+cf.name+L"\" of client \""+clientname+L"\" not found in database. Deleting it.", LL_WARNING);
					bool remove_folder=false;
					if(BackupServer::isSnapshotsEnabled())
					{
						if(!SnapshotHelper::removeFilesystem(clientname, cf.name) )
						{
							remove_folder=true;
						}
					}
					else
					{
						remove_folder=true;
					}

					if(remove_folder)
					{
						std::wstring rm_dir=backupfolder+os_file_sep()+clientname+os_file_sep()+cf.name;
						if(!os_remove_nonempty_dir(rm_dir))
						{
							Server->Log(L"Could not delete directory \""+rm_dir+L"\"", LL_ERROR);
						}
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
					if(ExtractFileName(res_images[k].path)==cf.name)
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

	Server->Log("Removing dangling file entries...", LL_INFO);
	cleanupdao->removeDanglingFiles();
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

bool ServerCleanupThread::cleanup_images_client(int clientid, int64 minspace, std::vector<int> &imageids)
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
	Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(full_image_num)+" full image backups max="+nconvert(max_image_full), LL_DEBUG);
	while(full_image_num>max_image_full)
	{
		ServerCleanupDAO::SImageBackupInfo res_info=cleanupdao->getImageBackupInfo(backupid);
		ServerCleanupDAO::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log(L"Deleting full image backup ( id="+convert(res_info.id)+L", backuptime="+res_info.backuptime+L", path="+res_info.path+L", letter="+res_info.letter+L" ) from client \""+clientname.value+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
		}

		if(!findUncompleteImageRef(backupid) )
		{
			std::vector<int> assoc=cleanupdao->getAssocImageBackups(backupid);
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
			else
			{
				imageids.push_back(backupid);
			}
				
		}
		else
		{
			Server->Log("Backup image has dependant image which is not complete");
			notit.push_back(backupid);
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
	Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(incr_image_num)+" incremental image backups max="+nconvert(max_image_incr), LL_DEBUG);
	while(incr_image_num>max_image_incr)
	{
		ServerCleanupDAO::SImageBackupInfo res_info=cleanupdao->getImageBackupInfo(backupid);
		ServerCleanupDAO::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log(L"Deleting incremental image backup ( id="+convert(res_info.id)+L", backuptime="+res_info.backuptime+L", path="+res_info.path+L", letter="+res_info.letter+L" ) from client \""+clientname.value+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
		}

		if(!findUncompleteImageRef(backupid) )
		{
			std::vector<int> assoc=cleanupdao->getAssocImageBackups(backupid);
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
			else
			{
				imageids.push_back(backupid);
			}
		}
		else
		{
			Server->Log("Backup image has dependant image which is not complete");
			notit.push_back(backupid);
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
	std::vector<ServerCleanupDAO::SIncompleteImages> incomplete_images=cleanupdao->getIncompleteImages();
	for(size_t i=0;i<incomplete_images.size();++i)
	{
		Server->Log(L"Deleting incomplete image file \""+incomplete_images[i].path+L"\"...", LL_INFO);
		if(!deleteImage(incomplete_images[i].path))
		{
			Server->Log(L"Deleting incomplete image \""+incomplete_images[i].path+L"\" failed.", LL_WARNING); 
		}
		cleanupdao->removeImage(incomplete_images[i].id);
	}

	{
		ServerSettings settings(db);
		int r=hasEnoughFreeSpace(minspace, &settings);
		if( r==-1 || r==1)
			return;
	}

	std::vector<int> res=cleanupdao->getClientsSortImagebackups();
	for(size_t i=0;i<res.size();++i)
	{
		int clientid=res[i];
		
		std::vector<int> imageids;
		if(cleanup_images_client(clientid, minspace, imageids))
		{
			if(minspace!=-1)
			{
				return;
			}
		}
	}
}

bool ServerCleanupThread::removeImage(int backupid, bool update_stat, int64 size_correction, bool force_remove)
{
	bool ret=true;

	ServerStatus::updateActive();

	std::vector<ServerCleanupDAO::SImageRef> refs=cleanupdao->getImageRefs(backupid);

	for(size_t i=0;i<refs.size();++i)
	{
		bool b=removeImage(refs[i].id, true, getImageSize(refs[i].id));
		if(!b)
			ret=false;
	}

	ServerCleanupDAO::CondString res=cleanupdao->getImagePath(backupid);
	if(res.exists)
	{
		_i64 stat_id;
		if(update_stat)
		{
			cleanupdao->addToImageStats(size_correction, backupid);
			stat_id=db->getLastInsertID();
		}

		if( deleteImage(res.value) || force_remove )
		{
			db->BeginTransaction();
			cleanupdao->removeImage(backupid);
			cleanupdao->removeImageSize(backupid);
			db->EndTransaction();
		}
		else
		{
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
	std::vector<ServerCleanupDAO::SImageRef> refs=cleanupdao->getImageRefs(backupid);

	for(size_t i=0;i<refs.size();++i)
	{
		if( refs[i].complete!=1 || findUncompleteImageRef(refs[i].id) )
			return true;
	}
	return false;
}

size_t ServerCleanupThread::getImagesFullNum(int clientid, int &backupid_top, const std::vector<int> &notit)
{
	std::vector<ServerCleanupDAO::SImageLetter> res=cleanupdao->getFullNumImages(clientid);
	std::map<std::wstring, std::vector<int> > images_ids;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring letter=res[i].letter;
		int cid=res[i].id;

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
	std::vector<ServerCleanupDAO::SImageLetter> res=cleanupdao->getIncrNumImages(clientid);
	std::map<std::wstring, std::vector<int> > images_ids;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring letter=res[i].letter;
		int cid=res[i].id;

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
	Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(full_file_num)+" full file backups max="+nconvert(max_file_full), LL_DEBUG);
	while(full_file_num>max_file_full )
	{
		ServerCleanupDAO::SFileBackupInfo res_info=cleanupdao->getFileBackupInfo(backupid);
		ServerCleanupDAO::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log(L"Deleting full file backup ( id="+convert(res_info.id)+L", backuptime="+res_info.backuptime+L", path="+res_info.path+L" ) from client \""+clientname.value+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
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
	Server->Log("Client with id="+nconvert(clientid)+" has "+nconvert(incr_file_num)+" incremental file backups max="+nconvert(max_file_incr), LL_DEBUG);
	while(incr_file_num>max_file_incr )
	{
		ServerCleanupDAO::SFileBackupInfo res_info=cleanupdao->getFileBackupInfo(backupid);
		ServerCleanupDAO::CondString clientname=cleanupdao->getClientName(clientid);
		if(clientname.exists && res_info.exists)
		{
			Server->Log(L"Deleting incremental file backup ( id="+convert(res_info.id)+L", backuptime="+res_info.backuptime+L", path="+res_info.path+L" ) from client \""+clientname.value+L"\" ( id="+convert(clientid)+L" ) ...", LL_INFO);
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

bool ServerCleanupThread::deleteFileBackup(const std::wstring &backupfolder, int clientid, int backupid, bool force_remove)
{
	ServerStatus::updateActive();

	ServerCleanupDAO::CondString cond_clientname=cleanupdao->getClientName(clientid);
	if(!cond_clientname.exists)
	{
		Server->Log("Error getting clientname in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}
	std::wstring &clientname=cond_clientname.value;

	ServerCleanupDAO::CondString cond_backuppath=cleanupdao->getFileBackupPath(backupid);

	if(!cond_backuppath.exists)
	{
		Server->Log("Error getting backuppath in ServerCleanupThread::deleteFileBackup", LL_ERROR);
		return false;
	}

	std::wstring backuppath=cond_backuppath.value;

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
		cleanupdao->moveFiles(backupid);
		cleanupdao->deleteFiles(backupid);
		cleanupdao->removeFileBackup(backupid);
		db->EndTransaction();
		db->AttachDBs();
	}

	ServerStatus::updateActive();
	
	return !err;
}

void ServerCleanupThread::removeClient(int clientid)
{
	std::wstring clientname=cleanupdao->getClientName(clientid).value;
	Server->Log(L"Deleting client with id \""+convert(clientid)+L"\" name \""+clientname+L"\"", LL_INFO);
	std::vector<ServerCleanupDAO::SImageBackupInfo> res_images;
	//remove image backups
	do
	{
		res_images=cleanupdao->getClientImages(clientid);

		if(!res_images.empty())
		{
			int backupid=res_images[0].id;
			Server->Log("Removing image with id \""+nconvert(backupid)+"\"", LL_INFO);
			removeImage(backupid, true, 0, true);
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
			Server->Log("Removing file backup with id \""+nconvert(backupid)+"\"", LL_INFO);
			bool b=deleteFileBackup(settings.getSettings()->backupfolder, clientid, backupid, true);
			if(b)
				Server->Log("Removing file backup with id \""+nconvert(backupid)+"\" successfull.", LL_INFO);
			else
				Server->Log("Removing file backup with id \""+nconvert(backupid)+"\" failed.", LL_ERROR);
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

int64 ServerCleanupThread::getImageSize(int backupid)
{
	ServerCleanupDAO::CondInt64 cond_res=cleanupdao->getImageSize(backupid);
	if(cond_res.exists)
	{
		return cond_res.value;
	}
	return -1;
}


void ServerCleanupThread::backup_database(void)
{
	ServerSettings settings(db);

	if(settings.getSettings()->backup_database)
	{
		Server->Log("Checking database integrity...", LL_INFO);
		db_results res = db->Read("PRAGMA quick_check");

		if(!res.empty() && res[0][L"integrity_check"]==L"ok")
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
		else
		{
			Server->Log("Database integrity check failed. Skipping Database backup.", LL_ERROR);
			Server->setFailBit(IServer::FAIL_DATABASE_CORRUPTED);
			BackupServerGet::sendMailToAdmins("Database integrity check failed", "Database integrity check failed before database backup. You should restore the UrBackup database from a backup or try to repair it.");
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

void ServerCleanupThread::enforce_quotas(void)
{
	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
	}

	std::vector<ServerCleanupDAO::SClientInfo> clients=cleanupdao->getClients();

	for(size_t i=0;i<clients.size();++i)
	{
		Server->Log("Enforcing quota for client \"" + Server->ConvertToUTF8(clients[i].name)+ "\" (id="+nconvert(clients[i].id)+")", LL_INFO);
		std::ostringstream log;
		log << "Quota enforcement report for client \"" << Server->ConvertToUTF8(clients[i].name) << "\" (id=" << clients[i].id  << ")" << std::endl;

		if(!enforce_quota(clients[i].id, log))
		{
			BackupServerGet::sendMailToAdmins("Quota enforcement failed", log.str());
			Server->Log(log.str(), LL_ERROR);
		}
		else
		{
			Server->Log(log.str(), LL_DEBUG);
		}
	}

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
		db->Write("PRAGMA shrink_memory");
	}
}

bool ServerCleanupThread::enforce_quota(int clientid, std::ostringstream& log)
{
	ServerSettings client_settings(db, clientid);

	if(client_settings.getSettings()->client_quota.empty())
	{
		log << "Client does not have a quota" << std::endl;
		return true;
	}

	bool did_remove_something=false;
	do
	{
		ServerCleanupDAO::CondInt64 used_storage=cleanupdao->getUsedStorage(clientid);
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
			std::wstring path=client_settings.getSettings()->backupfolder;
			int64 available_space=os_free_space(os_file_prefix(path));

			if(available_space==-1)
			{
				log << "Error getting free space -5" << std::endl;
				return false;
			}

			int64 space_to_free=used_storage.value-client_quota;

			int64 target_space=available_space-space_to_free;

			if(target_space<0)
			{
				log << "Error. Target space is negative" << std::endl;
				return false;
			}

			if(state==0)
			{
				std::vector<int> imageids;
				if(cleanup_images_client(clientid, target_space, imageids))
				{
					log << "Removed " << imageids.size() << " images with ids ";
					for(size_t i=0;i<imageids.size();++i) log << imageids[i];
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
				if(cleanup_one_filebackup_client(clientid, target_space, filebid))
				{
					log << "Removed file backup with id " << filebid << std::endl;

					did_remove_something=true;
					nopc=0;
					if(hasEnoughFreeSpace(target_space, &client_settings))
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


#endif //CLIENT_ONLY
