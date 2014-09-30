#include "server_update_stats.h"
#include "database.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include "server_settings.h"
#include "server_status.h"
#include "server_get.h"
#include "../Interface/DatabaseCursor.h"
#include "create_files_index.h"
#include <algorithm>

ServerUpdateStats::ServerUpdateStats(bool image_repair_mode, bool interruptible)
	: image_repair_mode(image_repair_mode), interruptible(interruptible)
{
}

void ServerUpdateStats::createQueries(void)
{
	q_get_images=db->Prepare("SELECT id,clientid,path FROM backup_images WHERE complete=1 AND running<datetime('now','-300 seconds')", false);
	q_update_images_size=db->Prepare("UPDATE clients SET bytes_used_images=? WHERE id=?", false);
	q_get_sizes=db->Prepare("SELECT id, bytes_used_files FROM clients", false);
	q_size_update=db->Prepare("UPDATE clients SET bytes_used_files=? WHERE id=?", false);
	q_update_backups=db->Prepare("UPDATE backups SET size_bytes=? WHERE id=?", false);
	q_get_backup_size=db->Prepare("SELECT size_bytes FROM backups WHERE id=?", false);
	q_get_del_size=db->Prepare("SELECT delsize FROM del_stats WHERE backupid=? AND image=0 AND created>datetime('now','-4 days')", false);
	q_add_del_size=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental, stoptime) VALUES (?, 0, ?, ?, ?, CURRENT_TIMESTAMP)", false);
	q_update_del_size=db->Prepare("UPDATE del_stats SET delsize=?,stoptime=CURRENT_TIMESTAMP WHERE backupid=? AND image=0 AND created>datetime('now','-4 days')", false);
	q_save_client_hist=db->Prepare("INSERT INTO clients_hist (id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, hist_id) SELECT id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, ? AS hist_id FROM clients", false);
	q_set_file_backup_null=db->Prepare("UPDATE backups SET size_bytes=0 WHERE size_bytes=-1 AND complete=1", false);
	q_create_hist=db->Prepare("INSERT INTO clients_hist_id (created) VALUES (CURRENT_TIMESTAMP)", false);
	q_get_all_clients=db->Prepare("SELECT id FROM clients", false);
}

void ServerUpdateStats::destroyQueries(void)
{
	db->destroyQuery(q_get_images);
	db->destroyQuery(q_update_images_size);
	db->destroyQuery(q_get_sizes);
	db->destroyQuery(q_size_update);
	db->destroyQuery(q_update_backups);
	db->destroyQuery(q_get_backup_size);
	db->destroyQuery(q_get_del_size);
	db->destroyQuery(q_add_del_size);
	db->destroyQuery(q_update_del_size);
	db->destroyQuery(q_save_client_hist);
	db->destroyQuery(q_set_file_backup_null);
	db->destroyQuery(q_create_hist);
	db->destroyQuery(q_get_all_clients);
}

void ServerUpdateStats::operator()(void)
{
	if(interruptible)
	{
		if( BackupServerGet::getNumberOfRunningFileBackups()>0 )
		{
			return;
		}
	}

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	backupdao.reset(new ServerBackupDao(db));
	fileindex.reset(create_lmdb_files_index());
	ServerSettings server_settings(db);
	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
	}

	createQueries();

	if(!image_repair_mode)
	{
		q_create_hist->Write();
		q_create_hist->Reset();

		q_save_client_hist->Bind(db->getLastInsertID());
		q_save_client_hist->Write();
		q_save_client_hist->Reset();
	}

	update_images();

	if(!image_repair_mode)
	{
		update_files();

		q_create_hist->Write();
		q_create_hist->Reset();

		q_save_client_hist->Bind(db->getLastInsertID());
		q_save_client_hist->Write();
		q_save_client_hist->Reset();

		q_set_file_backup_null->Write();
		q_set_file_backup_null->Reset();
	}

	destroyQueries();
	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
		db->freeMemory();
	}

	backupdao.reset();
}

void ServerUpdateStats::repairImages(void)
{
	ServerUpdateStats sus(true);
	sus();
}

void ServerUpdateStats::update_images(void)
{
	if(!image_repair_mode)
		Server->Log("Updating image stats...",LL_INFO);

	db_results res=q_get_images->Read();
	q_get_images->Reset();

	std::map<int, _i64> clients_used;

	db_results all_clients=q_get_all_clients->Read();
	q_get_all_clients->Reset();

	for(size_t i=0;i<all_clients.size();++i)
	{
		clients_used[watoi(all_clients[i][L"id"])]=0;
	}

	for(size_t i=0;i<res.size();++i)
	{
		std::wstring &fn=res[i][L"path"];
		IFile * file=Server->openFile(os_file_prefix(fn), MODE_READ);
		if(file==NULL)
		{
			bool b=repairImagePath(res[i]);
			if(b)
			{
				update_images();
				return;
			}
			Server->Log(L"Error opening file '"+fn+L"'", LL_ERROR);
			continue;
		}
		int cid=watoi(res[i][L"clientid"]);
		std::map<int, _i64>::iterator it=clients_used.find(cid);
		if(it==clients_used.end())
		{
			clients_used.insert(std::pair<int, _i64>(cid, file->Size()));
		}
		else
		{
			it->second+=file->Size();
		}
		Server->destroy(file);
	}

	for(std::map<int, _i64>::iterator it=clients_used.begin();it!=clients_used.end();++it)
	{
		q_update_images_size->Bind(it->second);
		q_update_images_size->Bind(it->first);
		q_update_images_size->Write();
		q_update_images_size->Reset();
	}
}

void ServerUpdateStats::measureSpeed(void)
{
	int64 ttime=Server->getTimeMS();
	static int64 lasttime=ttime;
	if(lasttime!=ttime)
	{
		float speed=num_updated_files/((ttime-lasttime)/1000.f);
		Server->Log("File processing speed: "+nconvert(speed)+" files/s", LL_INFO);
		num_updated_files=0;
		lasttime=ttime;
	}
}

void ServerUpdateStats::update_files(void)
{
	num_updated_files=0;
	
	Server->Log("Updating file statistics...");
	db_results res;

	size_t total_num = static_cast<size_t>(backupdao->getIncomingStatsCount().value);
	size_t total_i=0;

	std::map<int, _i64> size_data_clients=getFilebackupSizesClients();
	std::map<int, _i64> size_data_backups;
	std::map<int, SDelInfo> del_sizes;

	bool interrupted = false;
	bool started_transaction = false;

	int last_pc=0;
	do
	{
		if(interruptible)
		{
			if( BackupServerGet::getNumberOfRunningFileBackups()>0 )
			{
				interrupted=true;
				break;
			}
		}

		ServerStatus::updateActive();

		std::vector<ServerBackupDao::SIncomingStat> stat_entries = 
			backupdao->getIncomingStats();

		started_transaction=true;
		db->DetachDBs();
		db->BeginTransaction();

		for(size_t i=0;i<stat_entries.size();++i,++total_i)
		{
			++num_updated_files;

			if(total_i%1000==0 && total_i>0)
			{
				int pc=(std::min)(100, (int)((float)total_i/(float)total_num*100.f+0.5f));
				if(pc!=last_pc)
				{
					measureSpeed();
					Server->Log( "Updating file statistics: "+nconvert(pc)+"%", LL_INFO);
					last_pc=pc;
				}

				updateSizes(size_data_clients);
				updateDels(del_sizes);
				updateBackups(size_data_backups);

				db->EndTransaction();
				db->BeginTransaction();
			}

			ServerBackupDao::SIncomingStat& entry = stat_entries[i];

			std::vector<int> clients;
			std::vector<std::wstring> s_clients;
			Tokenize(entry.existing_clients, s_clients, L",");
			clients.resize(s_clients.size());
			for(size_t j=0;j<s_clients.size();++j)
			{
				clients[j]=watoi(s_clients[j]);
			}

			
			if(entry.direction==ServerBackupDao::c_direction_incoming)
			{
				int64 current_size_per_client = 0; 
				if(!clients.empty())
				{
					current_size_per_client=entry.filesize/clients.size();

					add(clients, -current_size_per_client, size_data_clients);
				}			

				clients.push_back(entry.clientid);
				current_size_per_client = entry.filesize/clients.size();
				
				add(clients, current_size_per_client, size_data_clients);

				add(size_data_backups, entry.backupid, entry.filesize);
			}
			else if(entry.direction==ServerBackupDao::c_direction_outgoing)
			{
				int64 current_size_per_client = entry.filesize/clients.size();

				add(clients, -current_size_per_client, size_data_clients);

				std::vector<int>::iterator it_client = std::find(clients.begin(), clients.end(), entry.clientid);
				if(it_client!=clients.end())
				{
					clients.erase(it_client);
				}

				if(!clients.empty())
				{
					current_size_per_client = entry.filesize/clients.size();

					add(clients, current_size_per_client, size_data_clients);
				}				

				add_del(del_sizes, entry.backupid, entry.filesize, entry.clientid, entry.incremental);
			}

			backupdao->delIncomingStatEntry(entry.id);
		}
	}
	while(!res.empty());

	updateSizes(size_data_clients);
	updateDels(del_sizes);
	updateBackups(size_data_backups);

	if(started_transaction)
	{
		db->EndTransaction();
		db->AttachDBs();	
	}

	backupdao->commit();

	if(!interrupted)
	{
		db->Write("UPDATE backups SET size_calculated=1 WHERE size_calculated=0 AND done=1");
	}
}

std::map<int, _i64> ServerUpdateStats::getFilebackupSizesClients(void)
{
	std::map<int, _i64> ret;
	db_results res=q_get_sizes->Read();
	q_get_sizes->Reset();
	for(size_t i=0;i<res.size();++i)
	{
		ret.insert(std::pair<int, _i64>(watoi(res[i][L"id"]), os_atoi64(wnarrow(res[i][L"bytes_used_files"]))));
	}
	return ret;
}

void ServerUpdateStats::updateSizes(std::map<int, _i64> & size_data)
{
	for(std::map<int, _i64>::iterator it=size_data.begin();it!=size_data.end();++it)
	{
		q_size_update->Bind(it->second);
		q_size_update->Bind(it->first);
		q_size_update->Write();
		q_size_update->Reset();
	}
}

void ServerUpdateStats::add(const std::vector<int>& subset, int64 num, std::map<int, _i64> &data)
{
	for(size_t i=0;i<subset.size();++i)
	{
		data[subset[i]]+=num;
	}
}

void ServerUpdateStats::add(std::map<int, _i64> &data, int backupid, _i64 filesize)
{
	std::map<int, _i64>::iterator it=data.find(backupid);
	if(it==data.end())
	{
		q_get_backup_size->Bind(backupid);
		db_results res=q_get_backup_size->Read();
		q_get_backup_size->Reset();
		if(!res.empty())
		{
			_i64 b_fs=os_atoi64(wnarrow(res[0][L"size_bytes"]));
			if(b_fs!=-1)
			{
				filesize+=b_fs;
			}
		}
		data.insert(std::pair<int, _i64>(backupid, filesize));
	}
	else
	{
		it->second+=filesize;
	}
}

void ServerUpdateStats::updateBackups(std::map<int, _i64> &data)
{
	for(std::map<int, _i64>::iterator it=data.begin();it!=data.end();++it)
	{
		q_update_backups->Bind(it->second);
		q_update_backups->Bind(it->first);
		q_update_backups->Write();
		q_update_backups->Reset();
	}
}

void ServerUpdateStats::add_del(std::map<int, SDelInfo> &data, int backupid, _i64 filesize, int clientid, int incremental)
{
	std::map<int, SDelInfo>::iterator it=data.find(backupid);
	if(it==data.end())
	{
		q_get_del_size->Bind(backupid);
		db_results res=q_get_del_size->Read();
		q_get_del_size->Reset();
		if(!res.empty())
		{
			filesize+=os_atoi64(wnarrow(res[0][L"delsize"]));
		}
		SDelInfo di;
		di.delsize=filesize;
		di.clientid=clientid;
		di.incremental=incremental;
		data.insert(std::pair<int, SDelInfo>(backupid, di));
	}
	else
	{
		it->second.delsize+=filesize;
	}
}

void ServerUpdateStats::updateDels(std::map<int, SDelInfo> &data)
{
	for(std::map<int, SDelInfo>::iterator it=data.begin();it!=data.end();++it)
	{
		q_get_del_size->Bind(it->first);
		db_results res=q_get_del_size->Read();
		q_get_del_size->Reset();
		if(res.empty())
		{
			q_add_del_size->Bind(it->first);
			q_add_del_size->Bind(it->second.delsize);
			q_add_del_size->Bind(it->second.clientid);
			q_add_del_size->Bind(it->second.incremental);
			q_add_del_size->Write();
			q_add_del_size->Reset();
		}
		else
		{
			q_update_del_size->Bind(it->second.delsize);
			q_update_del_size->Bind(it->first);
			q_update_del_size->Write();
			q_update_del_size->Reset();
		}
	}
}

bool ServerUpdateStats::repairImagePath(str_map img)
{
	int clientid=watoi(img[L"clientid"]);
	ServerSettings settings(db, clientid);
	IQuery *q=db->Prepare("SELECT name FROM clients WHERE id=?", false);
	q->Bind(clientid);
	db_results res=q->Read();
	q->Reset();
	db->destroyQuery(q);
	if(!res.empty())
	{
		std::wstring clientname=res[0][L"name"];
		std::wstring imgname=ExtractFileName(img[L"path"]);

		std::wstring new_name=settings.getSettings()->backupfolder+os_file_sep()+clientname+os_file_sep()+imgname;

		IFile * file=Server->openFile(os_file_prefix(new_name), MODE_READ);
		if(file==NULL)
		{
			Server->Log(L"Repairing image failed", LL_INFO);
			return false;
		}
		Server->destroy(file);

		q=db->Prepare("UPDATE backup_images SET path=? WHERE id=?", false);
		q->Bind(new_name);
		q->Bind(img[L"id"]);
		q->Write();
		q->Reset();
		db->destroyQuery(q);

		return true;
	}
	return false;
}
