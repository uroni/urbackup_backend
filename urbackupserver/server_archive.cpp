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

#include "server_archive.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "database.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "dao/ServerCleanupDao.h"
#include "ClientMain.h"
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
		archiveTimeoutFileBackups();
		archiveTimeoutImageBackups();
		archiveBackups();
		Server->clearDatabases(Server->getThreadID());
		IScopedLock lock(mutex);
		cond->wait(&lock, 60*60*1000);
	}

	delete this;
}

void ServerAutomaticArchive::archiveTimeoutFileBackups()
{
	IQuery *q_timeout=db->Prepare("SELECT id FROM backups WHERE archived=1 AND archive_timeout<>0 AND archive_timeout<?");
	if(q_timeout==NULL) return;

	q_timeout->Bind(Server->getTimeSeconds());
	db_results res_timeout=q_timeout->Read();
	
	IQuery *q_unarchive=db->Prepare("UPDATE backups SET archived=0 WHERE id=?");
	if(q_unarchive==NULL) return;
	for(size_t i=0;i<res_timeout.size();++i)
	{
		q_unarchive->Bind(res_timeout[i]["id"]);
		q_unarchive->Write();
		q_unarchive->Reset();
	}
}

void ServerAutomaticArchive::archiveTimeoutImageBackups()
{
	IQuery *q_timeout = db->Prepare("SELECT id FROM backup_images WHERE archived=1 AND archive_timeout<>0 AND archive_timeout<?");
	if (q_timeout == NULL) return;

	q_timeout->Bind(Server->getTimeSeconds());
	db_results res_timeout = q_timeout->Read();

	IQuery *q_unarchive = db->Prepare("UPDATE backup_images SET archived=0 WHERE id=?");
	if (q_unarchive == NULL) return;

	ServerCleanupDao cleanupdao(db);
	for (size_t i = 0; i<res_timeout.size(); ++i)
	{
		std::vector<int> tounarchive;
		int backupid = watoi(res_timeout[i]["id"]);
		tounarchive.push_back(backupid);
		std::vector<int> assoc_images = cleanupdao.getAssocImageBackups(backupid);
		tounarchive.insert(tounarchive.end(), assoc_images.begin(), assoc_images.end());
		assoc_images = cleanupdao.getAssocImageBackupsReverse(backupid);
		tounarchive.insert(tounarchive.end(), assoc_images.begin(), assoc_images.end());

		for (size_t j = 0; j < tounarchive.size(); ++j)
		{
			q_unarchive->Bind(tounarchive[j]);
			q_unarchive->Write();
			q_unarchive->Reset();
		}		
	}
}

void ServerAutomaticArchive::archiveBackups(void)
{
	IQuery *q_get_setting = db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=? AND key=?");
	IQuery *q_get_archived = db->Prepare("SELECT id, next_archival, interval, length, backup_types, archive_window, letters FROM settings_db.automatic_archival WHERE clientid=?");
	IQuery *q_get_letters = db->Prepare("SELECT DISTINCT letter FROM backup_images WHERE complete=1 AND archived=0 AND clientid=?");

	db_results res_clients=db->Read("SELECT id FROM clients");
	for(size_t i=0;i<res_clients.size();++i)
	{
		int clientid=watoi(res_clients[i]["id"]);
		q_get_setting->Bind(clientid);
		q_get_setting->Bind("archive_update");
		db_results res= q_get_setting->Read();
		q_get_setting->Reset();

		if (!res.empty() && res[0]["value"] == "1")
		{
			updateArchiveSettings(clientid);
		}
				
		q_get_archived->Bind(clientid);
		db_results res_archived=q_get_archived->Read();
		q_get_archived->Reset();
		
		for(size_t j=0;j<res_archived.size();++j)
		{
			_i64 next_archival=watoi64(res_archived[j]["next_archival"]);

			std::string &archive_window=res_archived[j]["archive_window"];
						
			_i64 curr_time=Server->getTimeSeconds();
			if(next_archival<curr_time && (archive_window.empty() || isInArchiveWindow(archive_window)) )
			{
				int backup_types = watoi(res_archived[j]["backup_types"]);
				bool image = (backup_types & (backup_type_full_image | backup_type_incr_image)) > 0;
				std::vector<std::string> letters;
				if (image)
				{
					std::string letter_str = res_archived[j]["letters"];
					if (strlower(trim(letter_str)) == "all")
					{	
						q_get_letters->Bind(clientid);
						db_results res = q_get_letters->Read();
						q_get_letters->Reset();
						for (size_t k = 0; k < res.size(); ++k)
						{
							if (!res[k]["letter"].empty()
								&& res[k]["letter"]!="SYSVOL"
								&& res[k]["letter"]!="ESP" )
							{
								letters.push_back(res[k]["letter"]);
							}
						}
					}
					else
					{
						Tokenize(letter_str, letters, ",;");
						for (size_t k = 0; k < res.size();)
						{
							if (letters[k].empty())
							{
								letters.erase(letters.begin() + k);
								continue;
							}

							letters[k] = ClientMain::normalizeVolumeUpper(letters[k]);

							++k;
						}
					}
				}
				else
				{
					letters.push_back(std::string());
				}
				for (size_t k = 0; k < letters.size(); ++k)
				{
					int backupid = getNonArchivedBackup(backup_types, clientid, letters[k]);
					if (backupid != 0)
					{
						int length = watoi(res_archived[j]["length"]);
						archiveBackup(backupid, length, image);
						Server->Log("Archived backup with id=" + convert(backupid) + " image=" + convert(image)+" letter="+letters[k]+ " for " + convert(length) + " seconds", LL_INFO);
						updateInterval(watoi(res_archived[j]["id"]), watoi(res_archived[j]["interval"]));
					}
					else
					{
						Server->Log("Did not find backup suitable for archiving with backup_type=" + convert(watoi(res_archived[j]["backup_types"])) + " image=" + convert(image)+" letter="+letters[k], LL_INFO);
					}
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

int ServerAutomaticArchive::getNonArchivedBackup(int backup_types, int clientid, const std::string& letter)
{
	int type_incr = backup_type_incr_file;
	int type_full = backup_type_full_file;

	if (!letter.empty())
	{
		type_incr = backup_type_incr_image;
		type_full = backup_type_full_image;
	}

	std::string incremental;
	if(backup_types & type_full && backup_types & type_incr)
		incremental="";
	else if( backup_types & type_incr )
		incremental=" AND incremental<>0";
	else if( backup_types & type_full)
		incremental=" AND incremental=0";

	std::string tbl = "backups";
	if (!letter.empty())
	{
		tbl = "backup_images";
		incremental += " AND letter=?";
	}
	IQuery *q_get_backups=db->Prepare("SELECT id FROM "+tbl+" WHERE complete=1 AND archived=0 AND clientid=?"+incremental+" ORDER BY backuptime DESC LIMIT 1");
	q_get_backups->Bind(clientid);
	if (!letter.empty())
	{
		q_get_backups->Bind(letter);
	}
	db_results res=q_get_backups->Read();
	if(!res.empty())
		return watoi(res[0]["id"]);
	else
		return 0;
}

void ServerAutomaticArchive::archiveBackup(int backupid, int length, bool image)
{
	std::vector<int> toarchive;
	toarchive.push_back(backupid);
	std::string tbl = "backups";
	if (image)
	{
		tbl = "backup_images";

		ServerCleanupDao cleanupdao(db);
		std::vector<int> assoc_images = cleanupdao.getAssocImageBackups(backupid);
		toarchive.insert(toarchive.end(), assoc_images.begin(), assoc_images.end());
		assoc_images = cleanupdao.getAssocImageBackupsReverse(backupid);
		toarchive.insert(toarchive.end(), assoc_images.begin(), assoc_images.end());
	}
	for (size_t i = 0; i < toarchive.size(); ++i)
	{
		IQuery *q_archive = db->Prepare("UPDATE " + tbl + " SET archived=1, archive_timeout=? WHERE id=?");
		if (length != -1)
		{
			q_archive->Bind(Server->getTimeSeconds() + length);
		}
		else
		{
			q_archive->Bind(-1);
		}
		q_archive->Bind(toarchive[i]);
		q_archive->Write();
		q_archive->Reset();
	}	
}

int ServerAutomaticArchive::getBackupTypes(const std::string &backup_type_name)
{
	int type=0;
	if (backup_type_name == "incr_file")
		type |= backup_type_incr_file;
	else if (backup_type_name == "full_file")
		type |= backup_type_full_file;
	else if (backup_type_name == "file")
		type |= backup_type_incr_file | backup_type_full_file;
	else if (backup_type_name == "image")
		type |= backup_type_full_image | backup_type_incr_image;
	else if (backup_type_name == "incr_image")
		type |= backup_type_incr_image;
	else if (backup_type_name == "full_image")
		type |= backup_type_full_image;

	return type;
}

std::string ServerAutomaticArchive::getBackupType(int backup_types)
{
	if ( (backup_types & backup_type_full_file) 
		&& (backup_types & backup_type_incr_file) )
		return "file";
	else if (backup_types & backup_type_full_file)
		return "full_file";
	else if (backup_types & backup_type_incr_file)
		return "incr_file";
	else if ( (backup_types & backup_type_full_image)
		&& (backup_types & backup_type_incr_image) )
		return "image";
	else if (backup_types & backup_type_full_image)
		return "full_image";
	else if (backup_types & backup_type_incr_image)
		return "incr_image";

	return "";
}

namespace
{
	bool nextArchiveIdx(str_map params, std::string& prefix, int& i, std::string& idx)
	{
		if (params.find("every_" + prefix + convert(i)) == params.end())
		{
			if (prefix == "d")
				prefix = "g";
			else if (prefix == "g")
				prefix = "c";
			else if (prefix == "c")
				return false;

			i = 0;

			if (params.find("every_" + prefix + convert(i)) == params.end())
			{
				idx = prefix + convert(i);
				return true;
			}
			else
			{
				return nextArchiveIdx(params, prefix, i, idx);
			}
		}

		idx = prefix + convert(i);

		return true;
	}

	struct SArchival
	{
		int64 next;
		int64 every;
		int64 archive_for;
		int backup_types;
		std::string every_unit;
		std::string for_unit;
		std::string window;
		std::string letters;
		std::string uuid;
	};
}

void ServerAutomaticArchive::updateArchiveSettings(int clientid)
{
	ServerSettings settings(db, clientid);

	str_map params;
	ParseParamStrHttp(settings.getSettings()->archive, &params);

	IQuery *q_next = db->Prepare("SELECT next_archival FROM settings_db.automatic_archival WHERE clientid=? AND uuid=?");

	IQuery *q_insert_all = db->Prepare("INSERT INTO settings_db.automatic_archival (next_archival, interval, interval_unit, length, length_unit, backup_types, clientid, archive_window, letters)"
		"VALUES (?,?,?,?,?,?,?,?,?)");

	std::string prefix = "d";
	std::string idx;

	std::vector<SArchival> new_settings;

	for (int i = 0; nextArchiveIdx(params, prefix, i, idx); ++i)
	{
		SArchival archive;
		archive.every = watoi64(params["every_" + idx]);
		archive.archive_for = watoi64(params["for_" + idx]);
		std::string backup_type_str = params["backup_type_" + idx];
		archive.backup_types = ServerAutomaticArchive::getBackupTypes(backup_type_str);
		archive.window = params["window_" + idx];
		archive.letters = params["window_" + idx];
		archive.every_unit = params["every_unit_" + idx];
		archive.for_unit = params["for_unit_" + idx];
		archive.uuid = hexToBytes(params["uuid_" + idx]);

		q_next->Bind(clientid);
		q_next->Bind(archive.uuid);

		db_results res_next = q_next->Read();

		q_next->Reset();

		archive.next = 0;
		if (!res_next.empty())
		{
			archive.next = watoi64(res_next[0]["next_archival"]);
		}

		if (archive.next == 0)
		{
			archive.next = Server->getTimeSeconds();
		}

		new_settings.push_back(archive);
	}


	DBScopedWriteTransaction trans(db);

	IQuery *q_del_all = db->Prepare("DELETE FROM settings_db.automatic_archival WHERE clientid=?");
	q_del_all->Bind(clientid);
	q_del_all->Write();


	for (size_t i = 0; i < new_settings.size(); ++i)
	{
		SArchival& archive = new_settings[i];

		q_insert_all->Bind(archive.next);
		q_insert_all->Bind(archive.every);
		q_insert_all->Bind(archive.every_unit);
		q_insert_all->Bind(archive.archive_for);
		q_insert_all->Bind(archive.for_unit);
		q_insert_all->Bind(archive.backup_types);
		q_insert_all->Bind(clientid);
		q_insert_all->Bind(archive.window);
		q_insert_all->Bind(archive.letters);
		q_insert_all->Write();
		q_insert_all->Reset();
	}

	IQuery *q_del_copied=db->Prepare("DELETE FROM settings_db.settings WHERE key='archive_update' AND clientid=?");
	q_del_copied->Bind(clientid);
	q_del_copied->Write();
	q_del_copied->Reset();
}

bool ServerAutomaticArchive::isInArchiveWindow(const std::string &window_def)
{
	std::vector<std::string> toks;
	Tokenize(window_def, toks, ";");
	bool matched_dom=false;
	bool has_dom = false;
	bool matched_dow = false;
	for(size_t i=0;i<toks.size();++i)
	{
		if(trim(toks[i])=="*")
			continue;

		std::vector<std::string> stoks;
		Tokenize(toks[i], stoks, ",");

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
			has_dom = true;
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
				if(i==3 && matched_dom)
					continue;

				return false;
			}
		}
		else
		{
			if(i==1) matched_dom=true;
			if (i == 3) matched_dow = true;
		}
	}

	if (has_dom
		&& !matched_dom
		&& !matched_dow)
	{
		return false;
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
