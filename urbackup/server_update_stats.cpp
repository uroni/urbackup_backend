#ifndef CLIENT_ONLY

#include "server_update_stats.h"
#include "database.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/File.h"
#include "os_functions.h"
#include "../stringtools.h"

void ServerUpdateStats::createQueries(void)
{
	q_get_images=db->Prepare("SELECT clientid,path FROM backup_images WHERE complete=1 AND running<datetime('now','-300 seconds')", false);
	q_update_images_size=db->Prepare("UPDATE clients SET bytes_used_images=? WHERE id=?", false);
	q_get_ncount_files=db->Prepare("SELECT files.rowid AS id, shahash, filesize, rsize, clientid, backupid FROM (files INNER JOIN backups ON files.backupid=backups.id) WHERE did_count=0 LIMIT 10000", false);
	q_has_client=db->Prepare("SELECT count(*) AS c FROM (files INNER JOIN backups ON files.backupid=backups.id) WHERE shahash=? AND filesize=? AND clientid=?", false);
	q_mark_done=db->Prepare("UPDATE files SET did_count=1 WHERE rowid=?", false);
	q_get_clients=db->Prepare("SELECT clientid, SUM(rsize) AS s_rsize FROM (files INNER JOIN backups ON files.backupid=backups.id) WHERE shahash=? AND filesize=? AND did_count=1 GROUP BY clientid", false);
	q_get_sizes=db->Prepare("SELECT id,bytes_used_files FROM clients", false);
	q_size_update=db->Prepare("UPDATE clients SET bytes_used_files=? WHERE id=?", false);
	q_get_delfiles=db->Prepare("SELECT files_del.rowid AS id, shahash, filesize, rsize, clientid, backupid, incremental FROM files_del LIMIT 10000", false);
	q_del_delfile=db->Prepare("DELETE FROM files_del WHERE rowid=?", false);
	q_update_backups=db->Prepare("UPDATE backups SET size_bytes=? WHERE id=?", false);
	q_get_backup_size=db->Prepare("SELECT size_bytes FROM backups WHERE id=?", false);
	q_get_del_size=db->Prepare("SELECT delsize FROM del_stats WHERE backupid=? AND image=0 AND created>datetime('now','-4 days')", false);
	q_add_del_size=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental, stoptime) VALUES (?, 0, ?, ?, ?, CURRENT_TIMESTAMP)", false);
	q_update_del_size=db->Prepare("UPDATE del_stats SET delsize=?,stoptime=CURRENT_TIMESTAMP WHERE backupid=? AND image=0 AND created>datetime('now','-4 days')", false);
	q_save_client_hist=db->Prepare("INSERT INTO clients_hist (id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, hist_id) SELECT id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, ? AS hist_id FROM clients", false);
	q_set_file_backup_null=db->Prepare("UPDATE backups SET size_bytes=0 WHERE size_bytes=-1 AND complete=1", false);
	q_transfer_bytes=db->Prepare("UPDATE files SET rsize=? WHERE rowid=?", false);
	q_get_transfer=db->Prepare("SELECT rowid AS id FROM files WHERE shahash=? AND filesize=? AND did_count=1 AND rsize=0 LIMIT 1", false);
	q_create_hist=db->Prepare("INSERT INTO clients_hist_id (created) VALUES (CURRENT_TIMESTAMP)", false);
	q_get_all_clients=db->Prepare("SELECT id FROM clients", false);
}

void ServerUpdateStats::destroyQueries(void)
{
	db->destroyQuery(q_get_images);
	db->destroyQuery(q_update_images_size);
	db->destroyQuery(q_get_ncount_files);
	db->destroyQuery(q_has_client);
	db->destroyQuery(q_mark_done);
	db->destroyQuery(q_get_clients);
	db->destroyQuery(q_get_sizes);
	db->destroyQuery(q_size_update);
	db->destroyQuery(q_get_delfiles);
	db->destroyQuery(q_del_delfile);
	db->destroyQuery(q_update_backups);
	db->destroyQuery(q_get_backup_size);
	db->destroyQuery(q_get_del_size);
	db->destroyQuery(q_add_del_size);
	db->destroyQuery(q_update_del_size);
	db->destroyQuery(q_save_client_hist);
	db->destroyQuery(q_set_file_backup_null);
	db->destroyQuery(q_transfer_bytes);
	db->destroyQuery(q_get_transfer);
	db->destroyQuery(q_create_hist);
	db->destroyQuery(q_get_all_clients);
}

void ServerUpdateStats::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	createQueries();

	q_create_hist->Write();
	q_create_hist->Reset();

	q_save_client_hist->Bind(db->getLastInsertID());
	q_save_client_hist->Write();
	q_save_client_hist->Reset();

	update_images();
	update_files();

	q_create_hist->Write();
	q_create_hist->Reset();

	q_save_client_hist->Bind(db->getLastInsertID());
	q_save_client_hist->Write();
	q_save_client_hist->Reset();

	q_set_file_backup_null->Write();
	q_set_file_backup_null->Reset();

	destroyQueries();
}

void ServerUpdateStats::update_images(void)
{
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
		IFile * file=Server->openFile(fn, MODE_READ);
		if(file==NULL)
		{
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

void ServerUpdateStats::update_files(void)
{
	Server->Log("Updating file stats...",LL_INFO);

	std::map<int, _i64> size_data=getSizes();
	unsigned int last_update_time=Server->getTimeMS();

	std::map<int, _i64> backup_sizes;

	db_results res;
	int last_pc=0;
	do
	{
		res=q_get_ncount_files->Read();
		q_get_ncount_files->Reset();
		for(size_t i=0;i<res.size();++i)
		{
			int pc=(int)((float)i/(float)res.size()*100.f+0.5f);
			if(pc!=last_pc)
			{
				Server->Log( "Updating files stats: "+nconvert(pc)+"%", LL_INFO);
				last_pc=pc;
			}

			_i64 rsize=os_atoi64(wnarrow(res[i][L"rsize"]));
			_i64 filesize=os_atoi64(wnarrow(res[i][L"filesize"]));
			_i64 id=os_atoi64(wnarrow(res[i][L"id"]));
			int backupid=watoi(res[i][L"backupid"]);
			int cid=watoi(res[i][L"clientid"]);
			std::wstring &shahash=res[i][L"shahash"];
			if(rsize==0)
			{
				q_has_client->Bind((char*)&shahash[0],(_u32)(shahash.size()*sizeof(wchar_t)));
				q_has_client->Bind(filesize);
				q_has_client->Bind(cid);
				db_results r=q_has_client->Read();
				q_has_client->Reset();
				if(!r.empty())
				{
					if(watoi(r[0][L"c"])>0)
					{
						q_mark_done->Bind(id);
						q_mark_done->Write();
						q_mark_done->Reset();
						continue;
					}
				}
			}
			else
			{
				add(backup_sizes, backupid, rsize);
			}
			_i64 nrsize;
			std::map<int, _i64> pre_sizes=calculateSizeDeltas(shahash, filesize, &nrsize);
			add(size_data, pre_sizes, -1);
			if(rsize!=0)
			{
				nrsize+=rsize;
			}
			size_t nmembers=pre_sizes.size();
			if(pre_sizes.find(cid)==pre_sizes.end())
			{
				++nmembers;
				pre_sizes.insert(std::pair<int, _i64>(cid, 0));
			}

			for(std::map<int, _i64>::iterator it=pre_sizes.begin();it!=pre_sizes.end();++it)
			{
				it->second=nrsize/nmembers;
			}
			add(size_data, pre_sizes, 1);

			q_mark_done->Bind(id);
			q_mark_done->Write();
			q_mark_done->Reset();

			if(Server->getTimeMS()-last_update_time>2000)
			{
				updateSizes(size_data);
				updateBackups(backup_sizes);
			}
		}
	}
	while(!res.empty());

	updateSizes(size_data);
	updateBackups(backup_sizes);


	std::map<int, SDelInfo> del_sizes;

	Server->Log("Updating deleted files...");
	last_pc=0;
	do
	{
		res=q_get_delfiles->Read();
		q_get_delfiles->Reset();
		for(size_t i=0;i<res.size();++i)
		{
			int pc=(int)((float)i/(float)res.size()*100.f+0.5f);
			if(pc!=last_pc)
			{
				Server->Log( "Updating del files stats: "+nconvert(pc)+"%", LL_INFO);
				last_pc=pc;
			}

			_i64 rsize=os_atoi64(wnarrow(res[i][L"rsize"]));
			_i64 filesize=os_atoi64(wnarrow(res[i][L"filesize"]));
			_i64 id=os_atoi64(wnarrow(res[i][L"id"]));
			int backupid=watoi(res[i][L"backupid"]);
			int cid=watoi(res[i][L"clientid"]);
			int incremental=watoi(res[i][L"incremental"]);
			std::wstring &shahash=res[i][L"shahash"];
			if(rsize==0)
			{
				q_has_client->Bind((char*)&shahash[0],(_u32)(shahash.size()*sizeof(wchar_t)));
				q_has_client->Bind(filesize);
				q_has_client->Bind(cid);
				db_results r=q_has_client->Read();
				q_has_client->Reset();
				if(!r.empty())
				{
					if(watoi(r[0][L"c"])>0)
					{
						q_del_delfile->Bind(id);
						q_del_delfile->Write();
						q_del_delfile->Reset();
						continue;
					}
				}
			}
			if(rsize!=0)
			{
				q_get_transfer->Bind((char*)&shahash[0],(_u32)(shahash.size()*sizeof(wchar_t)));
				q_get_transfer->Bind(filesize);
				db_results res=q_get_transfer->Read();
				q_get_transfer->Reset();
				if(!res.empty())
				{
					q_transfer_bytes->Bind(rsize);
					q_transfer_bytes->Bind(res[0][L"id"]);
					q_transfer_bytes->Write();
					q_transfer_bytes->Reset();
				}
			}

			_i64 nrsize;
			std::map<int, _i64> pre_sizes=calculateSizeDeltas(shahash, filesize, &nrsize);
			std::map<int, _i64> old_pre_sizes=pre_sizes;
			size_t nmembers=pre_sizes.size();
			if(nmembers==0)
			{
				add_del(del_sizes, backupid, rsize, cid, incremental);
				nrsize=rsize;
			}
			else
			{				
				add_del(del_sizes, backupid, 0, cid, incremental);
			}
			if(pre_sizes.find(cid)==pre_sizes.end())
			{
				++nmembers;
				pre_sizes.insert(std::pair<int, _i64>(cid, 0));
			}
			for(std::map<int, _i64>::iterator it=pre_sizes.begin();it!=pre_sizes.end();++it)
			{
				if(nmembers>0)
					it->second=nrsize/nmembers;
				else
					it->second=0;
			}
			add(size_data, pre_sizes, -1);			
			add(size_data, old_pre_sizes, 1);

			q_del_delfile->Bind(id);
			q_del_delfile->Write();
			q_del_delfile->Reset();

			if(Server->getTimeMS()-last_update_time>2000)
			{
				updateSizes(size_data);
				updateDels(del_sizes);
			}
		}
	}
	while(!res.empty());

	updateSizes(size_data);
	updateDels(del_sizes);
}

std::map<int, _i64> ServerUpdateStats::calculateSizeDeltas(const std::wstring &pShaHash, _i64 filesize,  _i64 *rsize)
{
	std::map<int, _i64> ret;
	q_get_clients->Bind((char*)&pShaHash[0],(_u32)(pShaHash.size()*sizeof(wchar_t)));
	q_get_clients->Bind(filesize);
	db_results res=q_get_clients->Read();
	q_get_clients->Reset();
	*rsize=0;
	for(size_t i=0;i<res.size();++i)
	{
		*rsize+=os_atoi64(wnarrow(res[i][L"s_rsize"]));
	}
	_i64 c_rsize=*rsize;
	if(!res.empty())
		c_rsize/=res.size();
	for(size_t i=0;i<res.size();++i)
	{
		ret.insert(std::pair<int, _i64>(watoi(res[i][L"clientid"]), c_rsize));
	}
	return ret;
}

std::map<int, _i64> ServerUpdateStats::getSizes(void)
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

void ServerUpdateStats::add(std::map<int, _i64> &data, std::map<int, _i64> &delta, int mod)
{
	for(std::map<int, _i64>::iterator it=delta.begin();it!=delta.end();++it)
	{
		std::map<int, _i64>::iterator iter=data.find(it->first);
		if(iter!=data.end())
		{
			if(mod==1)
			{
				iter->second+=it->second;
			}
			else
			{
				iter->second-=it->second;
			}
		}
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

#endif //CLIENT_ONLY