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

#include "server_archive.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "database.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <algorithm>
#include <stdlib.h>

ICondition *ServerAutomaticArchive::cond=NULL;
IMutex *ServerAutomaticArchive::mutex=NULL;
volatile bool ServerAutomaticArchive::do_quit=false;

void ServerAutomaticArchive::operator()(void)
{
	Server->waitForStartupComplete();

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);


	while(!do_quit)
	{
		archiveTimeout();
		archiveBackups();
		IScopedLock lock(mutex);
		cond->wait(&lock, 60*60*1000);
	}

	delete this;
}

void ServerAutomaticArchive::archiveTimeout(void)
{
	IQuery *q_timeout=db->Prepare("SELECT id FROM backups WHERE archived=1 AND archive_timeout<>0 AND archive_timeout<?");
	if(q_timeout==NULL) return;

	q_timeout->Bind(Server->getTimeSeconds());
	db_results res_timeout=q_timeout->Read();
	
	IQuery *q_unarchive=db->Prepare("UPDATE backups SET archived=0 WHERE id=?");
	if(q_unarchive==NULL) return;
	for(size_t i=0;i<res_timeout.size();++i)
	{
		q_unarchive->Bind(res_timeout[i][L"id"]);
		q_unarchive->Write();
		q_unarchive->Reset();
	}
}

void ServerAutomaticArchive::archiveBackups(void)
{
	db_results res_clients=db->Read("SELECT id FROM clients");
	for(size_t i=0;i<res_clients.size();++i)
	{
		int clientid=watoi(res_clients[i][L"id"]);
		int r_clientid=clientid;
		IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=? AND key=?");
		q_get->Bind(clientid);
		q_get->Bind("overwrite");
		db_results res=q_get->Read();
		q_get->Reset();
		if(res.empty() || res[0][L"value"]!=L"true")
			r_clientid=0;

		q_get->Bind(clientid);
		q_get->Bind("overwrite_archive_settings");
		res=q_get->Read();
		q_get->Reset();
		if(res.empty() || res[0][L"value"]!=L"true")
			r_clientid=0;

		bool archive_settings_copied=false;
		q_get->Bind(clientid);
		q_get->Bind("archive_settings_copied");
		res=q_get->Read();
		if(!res.empty() && res[0][L"value"]==L"true")
			archive_settings_copied=true;

		if(r_clientid==0 && !archive_settings_copied)
		{
			copyArchiveSettings(clientid);
		}

		IQuery *q_get_archived=db->Prepare("SELECT id, next_archival, interval, length, backup_types, archive_window FROM settings_db.automatic_archival WHERE clientid=?");
		q_get_archived->Bind(clientid);
		db_results res_archived=q_get_archived->Read();

		
		for(size_t j=0;j<res_archived.size();++j)
		{
			_i64 next_archival=watoi64(res_archived[j][L"next_archival"]);

			std::wstring &archive_window=res_archived[j][L"archive_window"];
						
			_i64 curr_time=Server->getTimeSeconds();
			if(next_archival<curr_time && (archive_window.empty() || isInArchiveWindow(archive_window)) )
			{
				int backupid=getNonArchivedFileBackup(watoi(res_archived[j][L"backup_types"]), clientid);
				if(backupid!=0)
				{
					int length=watoi(res_archived[j][L"length"]);
					archiveFileBackup(backupid, length);
					Server->Log("Archived file backup with id="+nconvert(backupid)+" for "+nconvert(length)+" seconds", LL_INFO);
					updateInterval(watoi(res_archived[j][L"id"]), watoi(res_archived[j][L"interval"]));
				}
				else
				{
					Server->Log("Did not find file backup suitable for archiving with backup_type="+nconvert(watoi(res_archived[j][L"backup_types"])), LL_INFO);
				}
			}
		}
	}
}

void ServerAutomaticArchive::updateInterval(int archiveid, int interval)
{
	IQuery *q_update_interval=db->Prepare("UPDATE settings_db.automatic_archival SET next_archival=? WHERE id=?");
	if(interval>0)
	{
		interval-=60;
	}
	q_update_interval->Bind(Server->getTimeSeconds()+interval);
	q_update_interval->Bind(archiveid);
	q_update_interval->Write();
}

int ServerAutomaticArchive::getNonArchivedFileBackup(int backup_types, int clientid)
{
	std::string incremental;
	if(backup_types & backup_type_full_file && backup_types & backup_type_incr_file)
		incremental="";
	else if( backup_types & backup_type_incr_file )
		incremental=" AND incremental<>0";
	else if( backup_types & backup_type_full_file)
		incremental=" AND incremental=0";

	IQuery *q_get_backups=db->Prepare("SELECT id FROM backups WHERE complete=1 AND archived=0 AND clientid=?"+incremental+" ORDER BY backuptime DESC LIMIT 1");
	q_get_backups->Bind(clientid);
	db_results res=q_get_backups->Read();
	if(!res.empty())
		return watoi(res[0][L"id"]);
	else
		return 0;
}

void ServerAutomaticArchive::archiveFileBackup(int backupid, int length)
{
	IQuery *q_archive=db->Prepare("UPDATE backups SET archived=1, archive_timeout=? WHERE id=?");
	if(length!=-1)
	{
		q_archive->Bind(Server->getTimeSeconds()+length);
	}
	else
	{
		q_archive->Bind(-1);
	}
	q_archive->Bind(backupid);
	q_archive->Write();
}

int ServerAutomaticArchive::getBackupTypes(const std::wstring &backup_type_name)
{
	int type=0;
	if(backup_type_name==L"incr_file")
		type|=backup_type_incr_file;
	else if(backup_type_name==L"full_file")
		type|=backup_type_full_file;
	else if(backup_type_name==L"file")
		type|=backup_type_incr_file|backup_type_full_file;

	return type;
}

std::wstring ServerAutomaticArchive::getBackupType(int backup_types)
{
	if( backup_types & backup_type_full_file && backup_types & backup_type_incr_file )
		return L"file";
	else if( backup_types & backup_type_full_file )
		return L"full_file";
	else if( backup_types & backup_type_incr_file)
		return L"incr_file";

	return L"";
}

void ServerAutomaticArchive::copyArchiveSettings(int clientid)
{
	db_results res_all=db->Read("SELECT id, next_archival, interval, interval_unit, length, length_unit, backup_types, archive_window FROM settings_db.automatic_archival WHERE clientid=0");


	std::vector<std::wstring> next_archivals;
	for(size_t i=0;i<res_all.size();++i)
	{
		std::wstring &interval=res_all[i][L"interval"];
		std::wstring &length=res_all[i][L"length"];
		std::wstring &backup_types=res_all[i][L"backup_types"];
		std::wstring &id=res_all[i][L"id"];
		std::wstring &archive_window=res_all[i][L"archive_window"];
		std::wstring next_archival=res_all[i][L"next_archival"];

		IQuery *q_next=db->Prepare("SELECT next_archival FROM settings_db.automatic_archival WHERE clientid=? AND interval=? AND length=? AND backup_types=? AND archive_window=?");
		IQuery *q_num=db->Prepare("SELECT count(*) AS num FROM settings_db.automatic_archival WHERE clientid=0 AND interval=? AND length=? AND backup_types=? AND archive_window=? AND id<?");

		q_num->Bind(interval);
		q_num->Bind(length);
		q_num->Bind(backup_types);
		q_num->Bind(archive_window);
		q_num->Bind(id);
		db_results res_num=q_num->Read();
		int num=watoi(res_num[0][L"num"]);

		q_next->Bind(clientid);
		q_next->Bind(interval);
		q_next->Bind(length);
		q_next->Bind(backup_types);
		q_next->Bind(archive_window);


		db_results res_next=q_next->Read();
		if((size_t)num<res_next.size())
		{
			next_archival=res_next[num][L"next_archival"];
			_i64 na=watoi64(next_archival);
			if(na==0)
			{
				next_archival=convert(Server->getTimeSeconds());
			}
		}

		next_archivals.push_back(next_archival);
	}


	IQuery *q_del_all=db->Prepare("DELETE FROM settings_db.automatic_archival WHERE clientid=?");
	IQuery *q_insert_all=db->Prepare("INSERT INTO settings_db.automatic_archival (next_archival, interval, interval_unit, length, length_unit, backup_types, clientid, archive_window)"
									"VALUES (?,?,?,?,?,?,?,?)");

	q_del_all->Bind(clientid);
	q_del_all->Write();

	for(size_t i=0;i<res_all.size();++i)
	{
		std::wstring &interval=res_all[i][L"interval"];
		std::wstring &length=res_all[i][L"length"];
		std::wstring &backup_types=res_all[i][L"backup_types"];		
		std::wstring &archive_window=res_all[i][L"archive_window"];

		q_insert_all->Bind(next_archivals[i]);
		q_insert_all->Bind(interval);
		q_insert_all->Bind(res_all[i][L"interval_unit"]);
		q_insert_all->Bind(length);
		q_insert_all->Bind(res_all[i][L"length_unit"]);
		q_insert_all->Bind(backup_types);
		q_insert_all->Bind(clientid);
		q_insert_all->Bind(archive_window);		
		q_insert_all->Write();
		q_insert_all->Reset();
	}	

	IQuery *q_del_copied=db->Prepare("DELETE FROM settings_db.settings WHERE key='archive_settings_copied' AND clientid=?");
	q_del_copied->Bind(clientid);
	q_del_copied->Write();
	q_del_copied->Reset();

	IQuery *q_insert_copied=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('archive_settings_copied','true',?)");
	q_insert_copied->Bind(clientid);
	q_insert_copied->Write();
	q_insert_copied->Reset();
}

bool ServerAutomaticArchive::isInArchiveWindow(const std::wstring &window_def)
{
	std::vector<std::wstring> toks;
	Tokenize(window_def, toks, L";");
	bool matched_dom=false;
	for(size_t i=0;i<toks.size();++i)
	{
		if(trim(toks[i])==L"*")
			continue;

		std::vector<std::wstring> stoks;
		Tokenize(toks[i], stoks, L",");

		std::vector<int> nums;
		for(size_t j=0;j<stoks.size();++j)
		{
			int n=watoi(stoks[j]);
			if(i==3)//dow
			{
				if(n==7) n=0;
			}
			nums.push_back(n);
		}

		int ref_num=-1;
		if(i==0) // hour
		{
			ref_num=atoi(os_strftime("%H").c_str());
		}
		else if(i==1) // dom
		{
			ref_num=atoi(os_strftime("%d").c_str());
		}
		else if(i==2) // mon
		{
			ref_num=atoi(os_strftime("%m").c_str());
		}
		else if(i==3) // dow
		{
			ref_num=atoi(os_strftime("%w").c_str());
			if(ref_num==7) ref_num=0;
		}

		if( std::find(nums.begin(), nums.end(), ref_num)==nums.end() )
		{
			if(i!=1)
			{
				if(i==3 && matched_dom==true)
					continue;

				return false;
			}
		}
		else
		{
			if(i==1) matched_dom=true;
		}
	}

	return true;
}

void ServerAutomaticArchive::doQuit(void)
{
	do_quit=true;
	IScopedLock lock(mutex);
	cond->notify_all();
}

void ServerAutomaticArchive::initMutex(void)
{
	mutex=Server->createMutex();
	cond=Server->createCondition();
}

void ServerAutomaticArchive::destroyMutex(void)
{
	Server->destroy(mutex);
	Server->destroy(cond);
}
