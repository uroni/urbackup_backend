#ifndef CLIENT_ONLY

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

bool update_stats_use_transactions_del=true;
const bool update_stats_use_transactions_done=false;
const bool update_stats_autocommit=true;
bool update_stats_bulk_done_files=true;
const bool update_stats_bulk_del_files=true;
const size_t client_sum_cache_size=100;
const size_t file_client_num_cache_size=100;
const size_t file_del_client_num_cache_size=100;

ServerUpdateStats::ServerUpdateStats(bool image_repair_mode, bool interruptible)
	: image_repair_mode(image_repair_mode), interruptible(interruptible)
{
}

void ServerUpdateStats::createQueries(void)
{
	q_get_images=db->Prepare("SELECT id,clientid,path FROM backup_images WHERE complete=1 AND running<datetime('now','-300 seconds')", false);
	q_update_images_size=db->Prepare("UPDATE clients SET bytes_used_images=? WHERE id=?", false);
	q_get_ncount_files=db->Prepare("SELECT rowid AS id, shahash, filesize, rsize, clientid, backupid FROM files INDEXED BY files_did_count WHERE did_count=0 LIMIT 10000", false);
	q_get_ncount_files_num=db->Prepare("SELECT count(*) AS c FROM files INDEXED BY files_did_count WHERE did_count=0", false);
	q_has_client=db->Prepare("SELECT count(*) AS c FROM files WHERE shahash=? AND filesize=? AND clientid=?", false);
	q_has_client_del=db->Prepare("SELECT count(*) AS c FROM files_del WHERE shahash=? AND filesize=? AND clientid=?", false);
	q_mark_done=db->Prepare("UPDATE files SET did_count=1 WHERE rowid=?", false);
	q_get_clients=db->Prepare("SELECT clientid, SUM(rsize) AS s_rsize FROM files WHERE shahash=? AND filesize=? AND +did_count=1 GROUP BY clientid", false);
	q_get_clients_del=db->Prepare("SELECT clientid, SUM(rsize) AS s_rsize FROM files_del WHERE shahash=? AND filesize=? GROUP BY clientid", false);
	q_get_sizes=db->Prepare("SELECT id,bytes_used_files FROM clients", false);
	q_size_update=db->Prepare("UPDATE clients SET bytes_used_files=? WHERE id=?", false);
	q_get_delfiles=db->Prepare("SELECT files_del.rowid AS id, shahash, filesize, rsize, clientid, backupid, incremental, is_del FROM files_del LIMIT 10000", false);
	q_get_delfiles_num=db->Prepare("SELECT count(*) AS c FROM files_del", false);
	q_del_delfile=db->Prepare("DELETE FROM files_del WHERE rowid=?", false);
	q_update_backups=db->Prepare("UPDATE backups SET size_bytes=? WHERE id=?", false);
	q_get_backup_size=db->Prepare("SELECT size_bytes FROM backups WHERE id=?", false);
	q_get_del_size=db->Prepare("SELECT delsize FROM del_stats WHERE backupid=? AND image=0 AND created>datetime('now','-4 days')", false);
	q_add_del_size=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental, stoptime) VALUES (?, 0, ?, ?, ?, CURRENT_TIMESTAMP)", false);
	q_update_del_size=db->Prepare("UPDATE del_stats SET delsize=?,stoptime=CURRENT_TIMESTAMP WHERE backupid=? AND image=0 AND created>datetime('now','-4 days')", false);
	q_save_client_hist=db->Prepare("INSERT INTO clients_hist (id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, hist_id) SELECT id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, ? AS hist_id FROM clients", false);
	q_set_file_backup_null=db->Prepare("UPDATE backups SET size_bytes=0 WHERE size_bytes=-1 AND complete=1", false);
	q_transfer_bytes=db->Prepare("UPDATE files SET rsize=? WHERE rowid=?", false);
	q_get_transfer=db->Prepare("SELECT rowid AS id FROM files WHERE shahash=? AND filesize=? AND +did_count=1 AND +rsize=0 LIMIT 1", false);
	q_create_hist=db->Prepare("INSERT INTO clients_hist_id (created) VALUES (CURRENT_TIMESTAMP)", false);
	q_get_all_clients=db->Prepare("SELECT id FROM clients", false);
	q_mark_done_bulk_files=db->Prepare("UPDATE files SET did_count=1 WHERE rowid IN ( SELECT rowid FROM files WHERE did_count=0 LIMIT 10000 )", false);
	q_del_delfile_bulk=db->Prepare("DELETE FROM files_del WHERE rowid IN ( SELECT rowid FROM files_del LIMIT ? )", false);
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
	db->destroyQuery(q_mark_done_bulk_files);
	db->destroyQuery(q_get_delfiles_num);
	db->destroyQuery(q_get_ncount_files_num);
	db->destroyQuery(q_del_delfile_bulk);
	db->destroyQuery(q_has_client_del);
	db->destroyQuery(q_get_clients_del);
}

void ServerUpdateStats::operator()(void)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerSettings server_settings(db);
	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
	}

	createQueries();

	Server->Log("Copying files from files_new table...", LL_DEBUG);

	bool indices_suspended=suspendFilesIndices(server_settings);

	db->Write("INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, created, rsize, did_count, clientid, incremental) "
			  "SELECT backupid, fullpath, hashpath, shahash, filesize, created, rsize, 0 AS did_count, clientid, incremental FROM files_new");

	Server->Log("Deleting contents of files_new table...", LL_DEBUG);
	db->Write("DELETE FROM files_new");

	if(indices_suspended)
	{
		createFilesIndices();
	}


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
		db->Write("PRAGMA shrink_memory");
	}
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
	unsigned int ttime=Server->getTimeMS();
	static unsigned int lasttime=ttime;
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
	std::map<int, _i64> size_data=getSizes();
	unsigned int last_update_time=Server->getTimeMS();
	unsigned int last_commit_time=Server->getTimeMS();
	num_updated_files=0;

	files_num_clients_del_cache.clear();
	files_num_clients_cache.clear();

	std::map<int, SDelInfo> del_sizes;
	
	if(db->getEngineName()=="bdb")
	{
		update_stats_bulk_done_files=false;
		update_stats_use_transactions_del=false;
	}

	if(!update_stats_autocommit)
	{
		db->Write("PRAGMA wal_autocheckpoint=0");
	}
	else
	{
		db->Write("PRAGMA wal_autocheckpoint=10000");
	}

	if(update_stats_use_transactions_del)
	{
		db->DetachDBs();
		db->BeginTransaction();
	}

	Server->Log("Updating deleted files...");
	db_results res;

	res=q_get_delfiles_num->Read();
	q_get_delfiles_num->Reset();

	size_t delfiles_num=0;
	if(res.size()>0)
		delfiles_num=watoi(res[0][L"c"]);

	size_t bulk_del=0;

	std::map< std::pair<std::wstring, _i64>, bool > already_del;

	size_t total_i=0;

	int last_pc=0;
	do
	{
		if(interruptible)
		{
			if( BackupServerGet::getNumberOfRunningFileBackups()>0 )
			{
				break;
			}
		}

		ServerStatus::updateActive();

		if(update_stats_use_transactions_del)
		{
			db->EndTransaction();
		}
		res=q_get_delfiles->Read();
		q_get_delfiles->Reset();
		if(update_stats_use_transactions_del)
		{
			db->BeginTransaction();
		}
		for(size_t i=0;i<res.size();++i,++total_i)
		{
			++num_updated_files;
			if(Server->getTimeMS()-last_update_time>2000)
			{
				updateSizes(size_data);
				updateDels(del_sizes);
				last_update_time=Server->getTimeMS();
			}

			if(update_stats_use_transactions_del && Server->getTimeMS()-last_commit_time>1000)
			{
				db->EndTransaction();
				db->BeginTransaction();
				last_commit_time=Server->getTimeMS();
			}

			if(delfiles_num>0)
			{
				int pc=(int)((float)total_i/(float)delfiles_num*100.f+0.5f);
				if(pc!=last_pc)
				{
					measureSpeed();
					Server->Log( "Updating del files stats: "+nconvert(pc)+"%", LL_INFO);
					last_pc=pc;
				}
			}

			_i64 rsize=os_atoi64(wnarrow(res[i][L"rsize"]));
			_i64 filesize=os_atoi64(wnarrow(res[i][L"filesize"]));
			_i64 id=os_atoi64(wnarrow(res[i][L"id"]));
			int backupid=watoi(res[i][L"backupid"]);
			int cid=watoi(res[i][L"clientid"]);
			int incremental=watoi(res[i][L"incremental"]);
			int is_del=watoi(res[i][L"is_del"]);
			std::wstring &shahash=res[i][L"shahash"];
			if(rsize==0)
			{
				SNumFilesClientCacheItem item(shahash, filesize, cid);
				size_t cnt=getFilesNumClient(item);
				
				if(cnt>0)
				{
					if(!update_stats_bulk_del_files)
					{
						q_del_delfile->Bind(id);
						q_del_delfile->Write();
						q_del_delfile->Reset();
					}
					else
					{
						++bulk_del;
					}
					continue;
				}

				cnt=getFilesNumClientDel(item);
				if(already_del.find(std::pair<std::wstring, _i64>(shahash, filesize)) != already_del.end() ||
						cnt>0)
				{
					if(!update_stats_bulk_del_files)
					{
						q_del_delfile->Bind(id);
						q_del_delfile->Write();
						q_del_delfile->Reset();
					}
					else
					{
						++bulk_del;
					}
					continue;
				}

				already_del[std::pair<std::wstring, _i64>(shahash, filesize)]=true;
			}

			_i64 size_add_nt=0;
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
					invalidateClientSum(shahash, filesize);
				}
				else
				{
					size_add_nt=rsize;
				}
			}

			q_del_delfile->Bind(id);
			q_del_delfile->Write();
			q_del_delfile->Reset();

			_i64 nrsize;
			std::map<int, _i64> pre_sizes=calculateSizeDeltas(shahash, filesize, &nrsize, true);
			nrsize+=size_add_nt;

			if(rsize!=0 && bulk_del)
			{
				already_deleted_bytes[SNumFilesClientCacheItem(shahash, filesize, cid)]+=filesize;
			}

			std::map<int, _i64> old_pre_sizes=pre_sizes;
			size_t nmembers=pre_sizes.size();
			if(nmembers==0)
			{
				if(is_del==1)
				{
					add_del(del_sizes, backupid, rsize, cid, incremental);
				}
				nrsize=rsize;
			}
			else if(is_del==1)
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
		}

		already_deleted_bytes.clear();
		already_del.clear();
		files_num_clients_del_cache.clear();
		files_num_clients_cache.clear();

		if(bulk_del>0)
		{
			q_del_delfile_bulk->Bind(bulk_del);
			q_del_delfile_bulk->Write();
			q_del_delfile_bulk->Reset();
			bulk_del=0;
		}

		if(!update_stats_autocommit)
		{
			if(update_stats_use_transactions_del)
			{
				db->EndTransaction();
			}
			Server->Log("Running wal checkpoint...", LL_DEBUG);
			db->Write("PRAGMA wal_checkpoint");
			Server->Log("done. (wal chkp)", LL_DEBUG);
			if(update_stats_use_transactions_del)
			{
				db->BeginTransaction();
			}
		}
	}
	while(!res.empty());

	updateSizes(size_data);
	updateDels(del_sizes);

	already_deleted_bytes.clear();

	if(update_stats_use_transactions_del)
	{
		db->EndTransaction();
		db->AttachDBs();
	}

	Server->Log("Updating file stats...",LL_INFO);

	last_update_time=Server->getTimeMS();

	std::map<int, _i64> backup_sizes;

	if(update_stats_use_transactions_done)
	{
		db->DetachDBs();
		db->BeginTransaction();
	}

	size_t ncount_files_num=0;
	res=q_get_ncount_files_num->Read();
	q_get_ncount_files_num->Reset();
	if(res.size()>0)
		ncount_files_num=watoi(res[0][L"c"]);
	
	last_pc=0;
	total_i=0;
	do
	{
		if(interruptible)
		{
			if( BackupServerGet::getNumberOfRunningFileBackups()>0 )
			{
				break;
			}
		}
		
		ServerStatus::updateActive();

		if(update_stats_use_transactions_done)
		{
			db->EndTransaction();
		}
		res=q_get_ncount_files->Read();
		q_get_ncount_files->Reset();
		if(update_stats_use_transactions_done)
		{
			db->BeginTransaction();
		}
		for(size_t i=0;i<res.size();++i,++total_i)
		{
			++num_updated_files;
			if(!update_stats_bulk_done_files && Server->getTimeMS()-last_update_time>2000)
			{
				updateSizes(size_data);
				updateBackups(backup_sizes);
				last_update_time=Server->getTimeMS();
			}

			if(update_stats_use_transactions_done && Server->getTimeMS()-last_commit_time>300)
			{
				db->EndTransaction();
				db->BeginTransaction();
				last_commit_time=Server->getTimeMS();
			}

			if(ncount_files_num>0)
			{
				int pc=(int)((float)total_i/(float)ncount_files_num*100.f+0.5f);
				if(pc!=last_pc)
				{
					measureSpeed();
					Server->Log( "Updating files stats: "+nconvert(pc)+"%", LL_INFO);
					last_pc=pc;
				}
			}

			_i64 rsize=os_atoi64(wnarrow(res[i][L"rsize"]));
			_i64 filesize=os_atoi64(wnarrow(res[i][L"filesize"]));
			_i64 id=os_atoi64(wnarrow(res[i][L"id"]));
			int backupid=watoi(res[i][L"backupid"]);
			int cid=watoi(res[i][L"clientid"]);
			std::wstring &shahash=res[i][L"shahash"];
			if(rsize==0)
			{
				SNumFilesClientCacheItem item(shahash, filesize, cid);
				size_t cnt=getFilesNumClient(item);
				
				if(cnt>0)
				{
					if(!update_stats_bulk_done_files)
					{
						q_mark_done->Bind(id);
						q_mark_done->Write();
						q_mark_done->Reset();
					}
					continue;
				}
			}
			else
			{
				add(backup_sizes, backupid, rsize);
			}
			_i64 nrsize;
			std::map<int, _i64> pre_sizes=calculateSizeDeltas(shahash, filesize, &nrsize, false);
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

			if(!update_stats_bulk_done_files)
			{
				q_mark_done->Bind(id);
				q_mark_done->Write();
				q_mark_done->Reset();
			}
		}

		files_num_clients_cache.clear();

		if(!update_stats_autocommit)
		{
			if(update_stats_use_transactions_done)
			{
				db->EndTransaction();
			}
			Server->Log("Running wal checkpoint...", LL_DEBUG);
			db->Write("PRAGMA wal_checkpoint");
			Server->Log("done.", LL_DEBUG);
			if(update_stats_use_transactions_done)
			{
				db->BeginTransaction();
			}
		}

		if(update_stats_bulk_done_files)
		{
			if(update_stats_use_transactions_done)
			{
				db->EndTransaction();
			}

			db->BeginTransaction();

			q_mark_done_bulk_files->Write();
			q_mark_done_bulk_files->Reset();
			updateSizes(size_data);
			updateBackups(backup_sizes);

			db->EndTransaction();

			if(update_stats_use_transactions_done)
			{
				db->BeginTransaction();
			}
		}
	}
	while(!res.empty());

	updateSizes(size_data);
	updateBackups(backup_sizes);

	if(update_stats_use_transactions_done)
	{
		db->EndTransaction();
		db->AttachDBs();
	}

	if(!update_stats_autocommit)
	{
		Server->Log("Running wal checkpoint...", LL_DEBUG);
		db->Write("PRAGMA wal_checkpoint");
		Server->Log("done.", LL_DEBUG);

		db->Write("PRAGMA wal_autocheckpoint=10000");
	}
	else
	{
		db->Write("PRAGMA wal_autocheckpoint=1000");
	}

	client_sum_cache.clear();
	files_num_clients_cache.clear();
	files_num_clients_del_cache.clear();

	db->Write("UPDATE backups SET size_calculated=1 WHERE size_calculated=0");
}

std::map<int, _i64> ServerUpdateStats::calculateSizeDeltas(const std::wstring &pShaHash, _i64 filesize,  _i64 *rsize, bool with_del)
{
	std::map<int, _i64> ret;
	std::vector<SClientSumCacheItem> items=getClientSum(pShaHash, filesize, with_del);
	if(with_del)
	{
		for(size_t i=0;i<items.size();++i)
		{
			std::map<SNumFilesClientCacheItem, _i64>::iterator it=already_deleted_bytes.find(SNumFilesClientCacheItem(pShaHash, filesize, items[i].clientid) );
			if(it!=already_deleted_bytes.end())
			{
				items[i].s_rsize-=it->second;
				if( items[i].s_rsize<=0 )
				{
					items.erase(items.begin()+i);
					--i;
				}
			}
		}
	}
	*rsize=0;
	for(size_t i=0;i<items.size();++i)
	{
		*rsize+=items[i].s_rsize;
	}
	_i64 c_rsize=*rsize;
	if(!items.empty())
		c_rsize/=items.size();
	for(size_t i=0;i<items.size();++i)
	{
		ret.insert(std::pair<int, _i64>(items[i].clientid, c_rsize));
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

std::vector<SClientSumCacheItem> ServerUpdateStats::getClientSum(const std::wstring &shahash, _i64 filesize, bool with_del)
{
	std::pair<std::wstring, _i64> key(shahash, filesize);
	std::map<std::pair<std::wstring, _i64>, std::vector<SClientSumCacheItem> >::iterator it=client_sum_cache.find(key);
	if(it!=client_sum_cache.end())
	{
		return it->second;
	}
	else
	{
		q_get_clients->Bind((char*)&shahash[0],(_u32)(shahash.size()*sizeof(wchar_t)));
		q_get_clients->Bind(filesize);
		db_results res=q_get_clients->Read();
		q_get_clients->Reset();

		std::vector<SClientSumCacheItem> ret;
		for(size_t i=0;i<res.size();++i)
		{
			ret.push_back(SClientSumCacheItem(watoi(res[i][L"clientid"]), os_atoi64(wnarrow(res[i][L"s_rsize"]))));
		}

		if(with_del)
		{
			q_get_clients_del->Bind((char*)&shahash[0],(_u32)(shahash.size()*sizeof(wchar_t)));
			q_get_clients_del->Bind(filesize);
			db_results res=q_get_clients_del->Read();
			q_get_clients_del->Reset();

			for(size_t i=0;i<res.size();++i)
			{
				int clientid=watoi(res[i][L"clientid"]);
				bool found=false;
				for(size_t j=0;j<ret.size();++j)
				{
					if(ret[j].clientid==clientid)
					{
						ret[j].s_rsize+=os_atoi64(wnarrow(res[i][L"s_rsize"]));
						found=true;
						break;
					}
				}

				if(!found)
				{
					ret.push_back(SClientSumCacheItem(clientid, os_atoi64(wnarrow(res[i][L"s_rsize"]))));
				}				
			}
		}

		if(client_sum_cache.size()>client_sum_cache_size)
		{
			client_sum_cache.clear();
		}

		client_sum_cache[key]=ret;

		return ret;
	}
}

void ServerUpdateStats::invalidateClientSum(const std::wstring &shahash, _i64 filesize)
{
	std::pair<std::wstring, _i64> key(shahash, filesize);
	std::map<std::pair<std::wstring, _i64>, std::vector<SClientSumCacheItem> >::iterator it=client_sum_cache.find(key);
	if(it!=client_sum_cache.end())
	{
		client_sum_cache.erase(it);
	}
}

size_t ServerUpdateStats::getFilesNumClient(const SNumFilesClientCacheItem &item)
{
	std::map<SNumFilesClientCacheItem, size_t>::iterator it=files_num_clients_cache.find(item);
	if(it!=files_num_clients_cache.end())
	{
		return it->second;
	}
	else
	{
		q_has_client->Bind((char*)&item.shahash[0],(_u32)(item.shahash.size()*sizeof(wchar_t)));
		q_has_client->Bind(item.filesize);
		q_has_client->Bind(item.clientid);
		db_results r=q_has_client->Read();
		q_has_client->Reset();
		if(r.size()>0)
		{
			size_t cnt=(size_t)watoi(r[0][L"c"]);
			
			if(files_num_clients_cache.size()>file_client_num_cache_size)
			{
				files_num_clients_cache.clear();
			}

			files_num_clients_cache[item]=cnt;
			return cnt;
		}
		else
		{
			return 0;
		}
	}
}

size_t ServerUpdateStats::getFilesNumClientDel(const SNumFilesClientCacheItem &item)
{
	std::map<SNumFilesClientCacheItem, size_t>::iterator it=files_num_clients_del_cache.find(item);
	if(it!=files_num_clients_del_cache.end())
	{
		return it->second;
	}
	else
	{
		q_has_client_del->Bind((char*)&item.shahash[0],(_u32)(item.shahash.size()*sizeof(wchar_t)));
		q_has_client_del->Bind(item.filesize);
		q_has_client_del->Bind(item.clientid);
		db_results r=q_has_client_del->Read();
		q_has_client_del->Reset();
		if(r.size()>0)
		{
			size_t cnt=(size_t)watoi(r[0][L"c"]);
			
			if(files_num_clients_del_cache.size()>file_del_client_num_cache_size)
			{
				files_num_clients_del_cache.clear();
			}

			files_num_clients_del_cache[item]=cnt;
			return cnt;
		}
		else
		{
			return 0;
		}
	}
}

bool ServerUpdateStats::suspendFilesIndices(ServerSettings& server_settings)
{
	db_results res_n=db->Read("SELECT COUNT(*) AS c FROM files_new");
	if(!res_n.empty() && watoi(res_n[0][L"c"])>=server_settings.getSettings()->suspend_index_limit)
	{
		Server->Log("Suspending files Indices...", LL_INFO);
		db->Write("DROP INDEX IF EXISTS files_idx");
		db->Write("DROP INDEX IF EXISTS files_did_count");
		db->Write("DROP INDEX IF EXISTS files_backupid");
		return true;
	}

	return false;
}

void ServerUpdateStats::createFilesIndices(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	Server->Log("Creating files Indices...", LL_INFO);
	db->Write("CREATE INDEX IF NOT EXISTS files_idx ON files (shahash, filesize, clientid)");
	db->Write("CREATE INDEX IF NOT EXISTS files_did_count ON files (did_count)");
	db->Write("CREATE INDEX IF NOT EXISTS files_backupid ON files (backupid)");
}

#endif //CLIENT_ONLY