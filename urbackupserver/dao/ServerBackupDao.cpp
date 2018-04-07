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

#include "ServerBackupDao.h"
#include "../../stringtools.h"
#include <assert.h>
#include <string.h>

int ServerBackupDao::num_issues_no_backuppaths = -1;

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TEMPORARY TABLE files_cont_path_lookup ( fullpath TEXT, entryid INTEGER);
*/

ServerBackupDao::ServerBackupDao( IDatabase *db )
	: db(db)
{
	prepareQueries();
}

ServerBackupDao::~ServerBackupDao()
{
	destroyQueries();
}


/**
* @-SQLGenAccess
* @func void ServerBackupDao::addToOldBackupfolders
* @sql
*      INSERT OR REPLACE INTO settings_db.old_backupfolders (backupfolder)
*          VALUES (:backupfolder(string))
*/
void ServerBackupDao::addToOldBackupfolders(const std::string& backupfolder)
{
	if(q_addToOldBackupfolders==NULL)
	{
		q_addToOldBackupfolders=db->Prepare("INSERT OR REPLACE INTO settings_db.old_backupfolders (backupfolder) VALUES (?)", false);
	}
	q_addToOldBackupfolders->Bind(backupfolder);
	q_addToOldBackupfolders->Write();
	q_addToOldBackupfolders->Reset();
}

/**
* @-SQLGenAccess
* @func vector<string> ServerBackupDao::getOldBackupfolders
* @return string backupfolder
* @sql
*     SELECT backupfolder FROM settings_db.old_backupfolders
*/
std::vector<std::string> ServerBackupDao::getOldBackupfolders(void)
{
	if(q_getOldBackupfolders==NULL)
	{
		q_getOldBackupfolders=db->Prepare("SELECT backupfolder FROM settings_db.old_backupfolders", false);
	}
	db_results res=q_getOldBackupfolders->Read();
	std::vector<std::string> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i]["backupfolder"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<string> ServerBackupDao::getDeletePendingClientNames
* @return string name
* @sql
*      SELECT name FROM clients WHERE delete_pending=1
*/
std::vector<std::string> ServerBackupDao::getDeletePendingClientNames(void)
{
	if(q_getDeletePendingClientNames==NULL)
	{
		q_getDeletePendingClientNames=db->Prepare("SELECT name FROM clients WHERE delete_pending=1", false);
	}
	db_results res=q_getDeletePendingClientNames->Read();
	std::vector<std::string> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i]["name"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getGroupName
* @return string name
* @sql
*      SELECT name FROM settings_db.si_client_groups WHERE id=:groupid(int)
*/
ServerBackupDao::CondString ServerBackupDao::getGroupName(int groupid)
{
	if(q_getGroupName==NULL)
	{
		q_getGroupName=db->Prepare("SELECT name FROM settings_db.si_client_groups WHERE id=?", false);
	}
	q_getGroupName->Bind(groupid);
	db_results res=q_getGroupName->Read();
	q_getGroupName->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["name"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerBackupDao::getClientGroup
* @return int groupid
* @sql
*      SELECT groupid FROM clients WHERE id=:clientid(int)
*/
ServerBackupDao::CondInt ServerBackupDao::getClientGroup(int clientid)
{
	if(q_getClientGroup==NULL)
	{
		q_getClientGroup=db->Prepare("SELECT groupid FROM clients WHERE id=?", false);
	}
	q_getClientGroup->Bind(clientid);
	db_results res=q_getClientGroup->Read();
	q_getClientGroup->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["groupid"]);
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func SClientName ServerBackupDao::getVirtualMainClientname
* @return string virtualmain, string name
* @sql
*      SELECT virtualmain, name FROM clients WHERE id=:clientid(int)
*/
ServerBackupDao::SClientName ServerBackupDao::getVirtualMainClientname(int clientid)
{
	if(q_getVirtualMainClientname==NULL)
	{
		q_getVirtualMainClientname=db->Prepare("SELECT virtualmain, name FROM clients WHERE id=?", false);
	}
	q_getVirtualMainClientname->Bind(clientid);
	db_results res=q_getVirtualMainClientname->Read();
	q_getVirtualMainClientname->Reset();
	SClientName ret = { false, "", "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.virtualmain=res[0]["virtualmain"];
		ret.name=res[0]["name"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::insertIntoOrigClientSettings
* @sql
*      INSERT OR REPLACE INTO orig_client_settings (clientid, data)
*          VALUES (:clientid(int), :data(std::string))
*/
void ServerBackupDao::insertIntoOrigClientSettings(int clientid, const std::string& data)
{
	if(q_insertIntoOrigClientSettings==NULL)
	{
		q_insertIntoOrigClientSettings=db->Prepare("INSERT OR REPLACE INTO orig_client_settings (clientid, data) VALUES (?, ?)", false);
	}
	q_insertIntoOrigClientSettings->Bind(clientid);
	q_insertIntoOrigClientSettings->Bind(data);
	q_insertIntoOrigClientSettings->Write();
	q_insertIntoOrigClientSettings->Reset();
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getOrigClientSettings
* @return string data
* @sql
*      SELECT data FROM orig_client_settings WHERE clientid = :clientid(int)
*/
ServerBackupDao::CondString ServerBackupDao::getOrigClientSettings(int clientid)
{
	if(q_getOrigClientSettings==NULL)
	{
		q_getOrigClientSettings=db->Prepare("SELECT data FROM orig_client_settings WHERE clientid = ?", false);
	}
	q_getOrigClientSettings->Bind(clientid);
	db_results res=q_getOrigClientSettings->Read();
	q_getOrigClientSettings->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["data"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SDuration> ServerBackupDao::getLastIncrementalDurations
* @return int64 indexing_time_ms, int64 duration 
* @sql
*      SELECT indexing_time_ms, (strftime('%s',running)-strftime('%s',backuptime)) AS duration
*		FROM backups 
*		WHERE clientid=:clientid(int) AND done=1 AND complete=1 AND incremental<>0 AND resumed=0
*		ORDER BY backuptime DESC LIMIT 10
*/
std::vector<ServerBackupDao::SDuration> ServerBackupDao::getLastIncrementalDurations(int clientid)
{
	if(q_getLastIncrementalDurations==NULL)
	{
		q_getLastIncrementalDurations=db->Prepare("SELECT indexing_time_ms, (strftime('%s',running)-strftime('%s',backuptime)) AS duration FROM backups  WHERE clientid=? AND done=1 AND complete=1 AND incremental<>0 AND resumed=0 ORDER BY backuptime DESC LIMIT 10", false);
	}
	q_getLastIncrementalDurations->Bind(clientid);
	db_results res=q_getLastIncrementalDurations->Read();
	q_getLastIncrementalDurations->Reset();
	std::vector<ServerBackupDao::SDuration> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].indexing_time_ms=watoi64(res[i]["indexing_time_ms"]);
		ret[i].duration=watoi64(res[i]["duration"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SDuration> ServerBackupDao::getLastFullDurations
* @return int64 indexing_time_ms, int64 duration 
* @sql
*      SELECT indexing_time_ms, (strftime('%s',running)-strftime('%s',backuptime)) AS duration
*		FROM backups 
*		WHERE clientid=:clientid(int) AND done=1 AND complete=1 AND incremental=0 AND resumed=0
*		ORDER BY backuptime DESC LIMIT 1
*/
std::vector<ServerBackupDao::SDuration> ServerBackupDao::getLastFullDurations(int clientid)
{
	if(q_getLastFullDurations==NULL)
	{
		q_getLastFullDurations=db->Prepare("SELECT indexing_time_ms, (strftime('%s',running)-strftime('%s',backuptime)) AS duration FROM backups  WHERE clientid=? AND done=1 AND complete=1 AND incremental=0 AND resumed=0 ORDER BY backuptime DESC LIMIT 1", false);
	}
	q_getLastFullDurations->Bind(clientid);
	db_results res=q_getLastFullDurations->Read();
	q_getLastFullDurations->Reset();
	std::vector<ServerBackupDao::SDuration> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].indexing_time_ms=watoi64(res[i]["indexing_time_ms"]);
		ret[i].duration=watoi64(res[i]["duration"]);
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func string ServerBackupDao::getClientSetting
* @return string value
* @sql
*      SELECT value FROM settings_db.settings WHERE key=:key(string) AND clientid=:clientid(int)
*/
ServerBackupDao::CondString ServerBackupDao::getClientSetting(const std::string& key, int clientid)
{
	if(q_getClientSetting==NULL)
	{
		q_getClientSetting=db->Prepare("SELECT value FROM settings_db.settings WHERE key=? AND clientid=?", false);
	}
	q_getClientSetting->Bind(key);
	q_getClientSetting->Bind(clientid);
	db_results res=q_getClientSetting->Read();
	q_getClientSetting->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["value"];
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func vector<int> ServerBackupDao::getClientIds
* @return int id
* @sql
*      SELECT id FROM clients
*/
std::vector<int> ServerBackupDao::getClientIds(void)
{
	if(q_getClientIds==NULL)
	{
		q_getClientIds=db->Prepare("SELECT id FROM clients", false);
	}
	db_results res=q_getClientIds->Read();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerBackupDao::getClientsByUid
* @return int id
* @sql
*      SELECT id FROM clients WHERE uid=:uid(string)
*/
std::vector<int> ServerBackupDao::getClientsByUid(const std::string& uid)
{
	if(q_getClientsByUid==NULL)
	{
		q_getClientsByUid=db->Prepare("SELECT id FROM clients WHERE uid=?", false);
	}
	q_getClientsByUid->Bind(uid);
	db_results res=q_getClientsByUid->Read();
	q_getClientsByUid->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::deleteClient
* @sql
*      DELETE FROM clients WHERE id=:clientid(int)
*/
void ServerBackupDao::deleteClient(int clientid)
{
	if(q_deleteClient==NULL)
	{
		q_deleteClient=db->Prepare("DELETE FROM clients WHERE id=?", false);
	}
	q_deleteClient->Bind(clientid);
	q_deleteClient->Write();
	q_deleteClient->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::changeClientName
* @sql
*      UPDATE clients SET name=:name(string), virtualmain=:virtualmain(string) WHERE id=:id(int)
*/
void ServerBackupDao::changeClientName(const std::string& name, const std::string& virtualmain, int id)
{
	if(q_changeClientName==NULL)
	{
		q_changeClientName=db->Prepare("UPDATE clients SET name=?, virtualmain=? WHERE id=?", false);
	}
	q_changeClientName->Bind(name);
	q_changeClientName->Bind(virtualmain);
	q_changeClientName->Bind(id);
	q_changeClientName->Write();
	q_changeClientName->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addClientMoved
* @sql
*      INSERT INTO moved_clients (from_name, to_name) VALUES (:from_name(string), :to_name(string))
*/
void ServerBackupDao::addClientMoved(const std::string& from_name, const std::string& to_name)
{
	if(q_addClientMoved==NULL)
	{
		q_addClientMoved=db->Prepare("INSERT INTO moved_clients (from_name, to_name) VALUES (?, ?)", false);
	}
	q_addClientMoved->Bind(from_name);
	q_addClientMoved->Bind(to_name);
	q_addClientMoved->Write();
	q_addClientMoved->Reset();
}

/**
* @-SQLGenAccess
* @func vector<string> ServerBackupDao::getClientMoved
* @return string from_name
* @sql
*	   SELECT from_name FROM moved_clients WHERE to_name=:to_name(string)
*/
std::vector<std::string> ServerBackupDao::getClientMoved(const std::string& to_name)
{
	if(q_getClientMoved==NULL)
	{
		q_getClientMoved=db->Prepare("SELECT from_name FROM moved_clients WHERE to_name=?", false);
	}
	q_getClientMoved->Bind(to_name);
	db_results res=q_getClientMoved->Read();
	q_getClientMoved->Reset();
	std::vector<std::string> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i]["from_name"];
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func string ServerBackupDao::getSetting
* @return string value
* @sql
*      SELECT value FROM settings_db.settings WHERE clientid=:clientid(int) AND key=:key(string)
*/
ServerBackupDao::CondString ServerBackupDao::getSetting(int clientid, const std::string& key)
{
	if(q_getSetting==NULL)
	{
		q_getSetting=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=? AND key=?", false);
	}
	q_getSetting->Bind(clientid);
	q_getSetting->Bind(key);
	db_results res=q_getSetting->Read();
	q_getSetting->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["value"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::insertSetting
* @sql
*      INSERT INTO settings_db.settings (key, value, clientid) VALUES ( :key(string), :value(string), :clientid(int) )
*/
void ServerBackupDao::insertSetting(const std::string& key, const std::string& value, int clientid)
{
	if(q_insertSetting==NULL)
	{
		q_insertSetting=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES ( ?, ?, ? )", false);
	}
	q_insertSetting->Bind(key);
	q_insertSetting->Bind(value);
	q_insertSetting->Bind(clientid);
	q_insertSetting->Write();
	q_insertSetting->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateSetting
* @sql
*      UPDATE settings_db.settings SET value=:value(string) WHERE key=:key(string) AND clientid=:clientid(int)
*/
void ServerBackupDao::updateSetting(const std::string& value, const std::string& key, int clientid)
{
	if(q_updateSetting==NULL)
	{
		q_updateSetting=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=?", false);
	}
	q_updateSetting->Bind(value);
	q_updateSetting->Bind(key);
	q_updateSetting->Bind(clientid);
	q_updateSetting->Write();
	q_updateSetting->Reset();
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getMiscValue
* @return string tvalue
* @sql
*       SELECT tvalue FROM misc WHERE tkey=:tkey(string)
*/
ServerBackupDao::CondString ServerBackupDao::getMiscValue(const std::string& tkey)
{
	if(q_getMiscValue==NULL)
	{
		q_getMiscValue=db->Prepare("SELECT tvalue FROM misc WHERE tkey=?", false);
	}
	q_getMiscValue->Bind(tkey);
	db_results res=q_getMiscValue->Read();
	q_getMiscValue->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["tvalue"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addMiscValue
* @sql
*       INSERT INTO misc (tkey, tvalue) VALUES (:tkey(string), :tvalue(string))
*/
void ServerBackupDao::addMiscValue(const std::string& tkey, const std::string& tvalue)
{
	if(q_addMiscValue==NULL)
	{
		q_addMiscValue=db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES (?, ?)", false);
	}
	q_addMiscValue->Bind(tkey);
	q_addMiscValue->Bind(tvalue);
	q_addMiscValue->Write();
	q_addMiscValue->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::delMiscValue
* @sql
*       DELETE FROM misc WHERE tkey=:tkey(string)
*/
void ServerBackupDao::delMiscValue(const std::string& tkey)
{
	if(q_delMiscValue==NULL)
	{
		q_delMiscValue=db->Prepare("DELETE FROM misc WHERE tkey=?", false);
	}
	q_delMiscValue->Bind(tkey);
	q_delMiscValue->Write();
	q_delMiscValue->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setClientUsedFilebackupSize
* @sql
*       UPDATE clients SET bytes_used_files=:bytes_used_files(int64) WHERE id=:id(int)
*/
void ServerBackupDao::setClientUsedFilebackupSize(int64 bytes_used_files, int id)
{
	if(q_setClientUsedFilebackupSize==NULL)
	{
		q_setClientUsedFilebackupSize=db->Prepare("UPDATE clients SET bytes_used_files=? WHERE id=?", false);
	}
	q_setClientUsedFilebackupSize->Bind(bytes_used_files);
	q_setClientUsedFilebackupSize->Bind(id);
	q_setClientUsedFilebackupSize->Write();
	q_setClientUsedFilebackupSize->Reset();
}

/**
* @-SQLGenAccess
* @func bool ServerBackupDao::newFileBackup
* @sql
*       INSERT INTO backups (incremental, clientid, path, complete, running, size_bytes, done, archived, size_calculated, resumed, indexing_time_ms, tgroup)
*		VALUES (:incremental(int), :clientid(int), :path(string), 0, CURRENT_TIMESTAMP, -1, 0, 0, 0, :resumed(int), :indexing_time_ms(int64), :tgroup(int) )
*/
bool ServerBackupDao::newFileBackup(int incremental, int clientid, const std::string& path, int resumed, int64 indexing_time_ms, int tgroup)
{
	if(q_newFileBackup==NULL)
	{
		q_newFileBackup=db->Prepare("INSERT INTO backups (incremental, clientid, path, complete, running, size_bytes, done, archived, size_calculated, resumed, indexing_time_ms, tgroup) VALUES (?, ?, ?, 0, CURRENT_TIMESTAMP, -1, 0, 0, 0, ?, ?, ? )", false);
	}
	q_newFileBackup->Bind(incremental);
	q_newFileBackup->Bind(clientid);
	q_newFileBackup->Bind(path);
	q_newFileBackup->Bind(resumed);
	q_newFileBackup->Bind(indexing_time_ms);
	q_newFileBackup->Bind(tgroup);
	bool ret = q_newFileBackup->Write();
	q_newFileBackup->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateFileBackupRunning
* @sql
*       UPDATE backups SET running=CURRENT_TIMESTAMP WHERE id=:backupid(int)
*/
void ServerBackupDao::updateFileBackupRunning(int backupid)
{
	if(q_updateFileBackupRunning==NULL)
	{
		q_updateFileBackupRunning=db->Prepare("UPDATE backups SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	}
	q_updateFileBackupRunning->Bind(backupid);
	q_updateFileBackupRunning->Write();
	q_updateFileBackupRunning->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setFileBackupDone
* @sql
*       UPDATE backups SET done=1 WHERE id=:backupid(int)
*/
void ServerBackupDao::setFileBackupDone(int backupid)
{
	if(q_setFileBackupDone==NULL)
	{
		q_setFileBackupDone=db->Prepare("UPDATE backups SET done=1 WHERE id=?", false);
	}
	q_setFileBackupDone->Bind(backupid);
	q_setFileBackupDone->Write();
	q_setFileBackupDone->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setFileBackupSynced
* @sql
*       UPDATE backups SET synctime=strftime('%s', 'now') WHERE id=:backupid(int)
*/
void ServerBackupDao::setFileBackupSynced(int backupid)
{
	if(q_setFileBackupSynced==NULL)
	{
		q_setFileBackupSynced=db->Prepare("UPDATE backups SET synctime=strftime('%s', 'now') WHERE id=?", false);
	}
	q_setFileBackupSynced->Bind(backupid);
	q_setFileBackupSynced->Write();
	q_setFileBackupSynced->Reset();
}

/**
* @-SQLGenAccess
* @func SLastIncremental ServerBackupDao::getLastIncrementalFileBackup
* @return int incremental, string path, int resumed, int complete, int id
* @sql
*       SELECT incremental, path, resumed, complete, id FROM backups WHERE clientid=:clientid(int) AND tgroup=:tgroup(int) AND done=1 ORDER BY backuptime DESC LIMIT 1
*/
ServerBackupDao::SLastIncremental ServerBackupDao::getLastIncrementalFileBackup(int clientid, int tgroup)
{
	if(q_getLastIncrementalFileBackup==NULL)
	{
		q_getLastIncrementalFileBackup=db->Prepare("SELECT incremental, path, resumed, complete, id FROM backups WHERE clientid=? AND tgroup=? AND done=1 ORDER BY backuptime DESC LIMIT 1", false);
	}
	q_getLastIncrementalFileBackup->Bind(clientid);
	q_getLastIncrementalFileBackup->Bind(tgroup);
	db_results res=q_getLastIncrementalFileBackup->Read();
	q_getLastIncrementalFileBackup->Reset();
	SLastIncremental ret = { false, 0, "", 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.incremental=watoi(res[0]["incremental"]);
		ret.path=res[0]["path"];
		ret.resumed=watoi(res[0]["resumed"]);
		ret.complete=watoi(res[0]["complete"]);
		ret.id=watoi(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SLastIncremental ServerBackupDao::getLastIncrementalCompleteFileBackup
* @return int incremental, string path, int resumed, int complete, int id
* @sql
*       SELECT incremental, path, resumed, complete, id FROM backups WHERE clientid=:clientid(int) AND tgroup=:tgroup(int) AND done=1 AND complete=1 
*                ORDER BY backuptime DESC LIMIT 1
*/
ServerBackupDao::SLastIncremental ServerBackupDao::getLastIncrementalCompleteFileBackup(int clientid, int tgroup)
{
	if(q_getLastIncrementalCompleteFileBackup==NULL)
	{
		q_getLastIncrementalCompleteFileBackup=db->Prepare("SELECT incremental, path, resumed, complete, id FROM backups WHERE clientid=? AND tgroup=? AND done=1 AND complete=1  ORDER BY backuptime DESC LIMIT 1", false);
	}
	q_getLastIncrementalCompleteFileBackup->Bind(clientid);
	q_getLastIncrementalCompleteFileBackup->Bind(tgroup);
	db_results res=q_getLastIncrementalCompleteFileBackup->Read();
	q_getLastIncrementalCompleteFileBackup->Reset();
	SLastIncremental ret = { false, 0, "", 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.incremental=watoi(res[0]["incremental"]);
		ret.path=res[0]["path"];
		ret.resumed=watoi(res[0]["resumed"]);
		ret.complete=watoi(res[0]["complete"]);
		ret.id=watoi(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateFileBackupSetComplete
* @sql
*       UPDATE backups SET complete=1 WHERE id=:backupid(int)
*/
void ServerBackupDao::updateFileBackupSetComplete(int backupid)
{
	if(q_updateFileBackupSetComplete==NULL)
	{
		q_updateFileBackupSetComplete=db->Prepare("UPDATE backups SET complete=1 WHERE id=?", false);
	}
	q_updateFileBackupSetComplete->Bind(backupid);
	q_updateFileBackupSetComplete->Write();
	q_updateFileBackupSetComplete->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::saveBackupLog
* @sql
*       INSERT INTO logs (clientid, errors, warnings, infos, image, incremental, resumed, restore)
*               VALUES (:clientid(int), :errors(int), :warnings(int), :infos(int), :image(int), :incremental(int), :resumed(int), :restore(int))
*/
void ServerBackupDao::saveBackupLog(int clientid, int errors, int warnings, int infos, int image, int incremental, int resumed, int restore)
{
	if(q_saveBackupLog==NULL)
	{
		q_saveBackupLog=db->Prepare("INSERT INTO logs (clientid, errors, warnings, infos, image, incremental, resumed, restore) VALUES (?, ?, ?, ?, ?, ?, ?, ?)", false);
	}
	q_saveBackupLog->Bind(clientid);
	q_saveBackupLog->Bind(errors);
	q_saveBackupLog->Bind(warnings);
	q_saveBackupLog->Bind(infos);
	q_saveBackupLog->Bind(image);
	q_saveBackupLog->Bind(incremental);
	q_saveBackupLog->Bind(resumed);
	q_saveBackupLog->Bind(restore);
	q_saveBackupLog->Write();
	q_saveBackupLog->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::saveBackupLogData
* @sql
*       INSERT INTO log_data (logid, data)
*               VALUES (:logid(int64), :data(string) )
*/
void ServerBackupDao::saveBackupLogData(int64 logid, const std::string& data)
{
	if(q_saveBackupLogData==NULL)
	{
		q_saveBackupLogData=db->Prepare("INSERT INTO log_data (logid, data) VALUES (?, ? )", false);
	}
	q_saveBackupLogData->Bind(logid);
	q_saveBackupLogData->Bind(data);
	q_saveBackupLogData->Write();
	q_saveBackupLogData->Reset();
}

/**
* @-SQLGenAccess
* @func vector<int> ServerBackupDao::getMailableUserIds
* @return int id
* @sql
*       SELECT id FROM settings_db.si_users WHERE report_mail IS NOT NULL AND report_mail<>''
*/
std::vector<int> ServerBackupDao::getMailableUserIds(void)
{
	if(q_getMailableUserIds==NULL)
	{
		q_getMailableUserIds=db->Prepare("SELECT id FROM settings_db.si_users WHERE report_mail IS NOT NULL AND report_mail<>''", false);
	}
	db_results res=q_getMailableUserIds->Read();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getUserRight
* @return string t_right
* @sql
*       SELECT t_right FROM settings_db.si_permissions WHERE clientid=:clientid(int) AND t_domain=:t_domain(string)
*/
ServerBackupDao::CondString ServerBackupDao::getUserRight(int clientid, const std::string& t_domain)
{
	if(q_getUserRight==NULL)
	{
		q_getUserRight=db->Prepare("SELECT t_right FROM settings_db.si_permissions WHERE clientid=? AND t_domain=?", false);
	}
	q_getUserRight->Bind(clientid);
	q_getUserRight->Bind(t_domain);
	db_results res=q_getUserRight->Read();
	q_getUserRight->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["t_right"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SReportSettings ServerBackupDao::getUserReportSettings
* @return string report_mail, int report_loglevel, int report_sendonly
* @sql
*       SELECT report_mail, report_loglevel, report_sendonly FROM settings_db.si_users WHERE id=:userid(int)
*/
ServerBackupDao::SReportSettings ServerBackupDao::getUserReportSettings(int userid)
{
	if(q_getUserReportSettings==NULL)
	{
		q_getUserReportSettings=db->Prepare("SELECT report_mail, report_loglevel, report_sendonly FROM settings_db.si_users WHERE id=?", false);
	}
	q_getUserReportSettings->Bind(userid);
	db_results res=q_getUserReportSettings->Read();
	q_getUserReportSettings->Reset();
	SReportSettings ret = { false, "", 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.report_mail=res[0]["report_mail"];
		ret.report_loglevel=watoi(res[0]["report_loglevel"]);
		ret.report_sendonly=watoi(res[0]["report_sendonly"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::formatUnixtime
* @return string time
* @sql
*       SELECT datetime(:unixtime(int64), 'unixepoch', 'localtime') AS time
*/
ServerBackupDao::CondString ServerBackupDao::formatUnixtime(int64 unixtime)
{
	if(q_formatUnixtime==NULL)
	{
		q_formatUnixtime=db->Prepare("SELECT datetime(?, 'unixepoch', 'localtime') AS time", false);
	}
	q_formatUnixtime->Bind(unixtime);
	db_results res=q_formatUnixtime->Read();
	q_formatUnixtime->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["time"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SImageBackup ServerBackupDao::getLastFullImage
* @return int64 id, int incremental, string path, int64 duration
* @sql
*       SELECT id, incremental, path, (strftime('%s',running)-strftime('%s',backuptime)) AS duration FROM backup_images
*         WHERE clientid=:clientid(int) AND incremental=0 AND complete=1 AND version=:image_version(int) AND letter=:letter(string) ORDER BY backuptime DESC LIMIT 1
*/
ServerBackupDao::SImageBackup ServerBackupDao::getLastFullImage(int clientid, int image_version, const std::string& letter)
{
	if(q_getLastFullImage==NULL)
	{
		q_getLastFullImage=db->Prepare("SELECT id, incremental, path, (strftime('%s',running)-strftime('%s',backuptime)) AS duration FROM backup_images WHERE clientid=? AND incremental=0 AND complete=1 AND version=? AND letter=? ORDER BY backuptime DESC LIMIT 1", false);
	}
	q_getLastFullImage->Bind(clientid);
	q_getLastFullImage->Bind(image_version);
	q_getLastFullImage->Bind(letter);
	db_results res=q_getLastFullImage->Read();
	q_getLastFullImage->Reset();
	SImageBackup ret = { false, 0, 0, "", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.incremental=watoi(res[0]["incremental"]);
		ret.path=res[0]["path"];
		ret.duration=watoi64(res[0]["duration"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SImageBackup ServerBackupDao::getLastImage
* @return int64 id, int incremental, string path, int64 duration
* @sql
*       SELECT id, incremental, path, (strftime('%s',running)-strftime('%s',backuptime)) AS duration FROM backup_images
*         WHERE clientid=:clientid(int) AND complete=1 AND version=:image_version(int) AND letter=:letter(string) ORDER BY backuptime DESC LIMIT 1
*/
ServerBackupDao::SImageBackup ServerBackupDao::getLastImage(int clientid, int image_version, const std::string& letter)
{
	if(q_getLastImage==NULL)
	{
		q_getLastImage=db->Prepare("SELECT id, incremental, path, (strftime('%s',running)-strftime('%s',backuptime)) AS duration FROM backup_images WHERE clientid=? AND complete=1 AND version=? AND letter=? ORDER BY backuptime DESC LIMIT 1", false);
	}
	q_getLastImage->Bind(clientid);
	q_getLastImage->Bind(image_version);
	q_getLastImage->Bind(letter);
	db_results res=q_getLastImage->Read();
	q_getLastImage->Reset();
	SImageBackup ret = { false, 0, 0, "", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.incremental=watoi(res[0]["incremental"]);
		ret.path=res[0]["path"];
		ret.duration=watoi64(res[0]["duration"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func bool ServerBackupDao::newImageBackup
* @sql
*       INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes, version, letter, backuptime, archived)
*			VALUES (:clientid(int), :path(string), :incremental(int), :incremental_ref(int), 0, CURRENT_TIMESTAMP,
*					0, :image_version(int), :letter(string), datetime(:backuptime(int64), 'unixepoch'), 0 )
*/
bool ServerBackupDao::newImageBackup(int clientid, const std::string& path, int incremental, int incremental_ref, int image_version, const std::string& letter, int64 backuptime)
{
	if(q_newImageBackup==NULL)
	{
		q_newImageBackup=db->Prepare("INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes, version, letter, backuptime, archived) VALUES (?, ?, ?, ?, 0, CURRENT_TIMESTAMP, 0, ?, ?, datetime(?, 'unixepoch'), 0 )", false);
	}
	q_newImageBackup->Bind(clientid);
	q_newImageBackup->Bind(path);
	q_newImageBackup->Bind(incremental);
	q_newImageBackup->Bind(incremental_ref);
	q_newImageBackup->Bind(image_version);
	q_newImageBackup->Bind(letter);
	q_newImageBackup->Bind(backuptime);
	bool ret = q_newImageBackup->Write();
	q_newImageBackup->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setImageSize
* @sql
*       UPDATE backup_images SET size_bytes=:size_bytes(int64) WHERE id=:backupid(int)
*/
void ServerBackupDao::setImageSize(int64 size_bytes, int backupid)
{
	if(q_setImageSize==NULL)
	{
		q_setImageSize=db->Prepare("UPDATE backup_images SET size_bytes=? WHERE id=?", false);
	}
	q_setImageSize->Bind(size_bytes);
	q_setImageSize->Bind(backupid);
	q_setImageSize->Write();
	q_setImageSize->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addImageSizeToClient
* @sql
*       UPDATE clients SET bytes_used_images=(SELECT bytes_used_images FROM clients WHERE id=:clientid(int))+:add_size(int64) WHERE id=:clientid(int)
*/
void ServerBackupDao::addImageSizeToClient(int clientid, int64 add_size)
{
	if(q_addImageSizeToClient==NULL)
	{
		q_addImageSizeToClient=db->Prepare("UPDATE clients SET bytes_used_images=(SELECT bytes_used_images FROM clients WHERE id=?)+? WHERE id=?", false);
	}
	q_addImageSizeToClient->Bind(clientid);
	q_addImageSizeToClient->Bind(add_size);
	q_addImageSizeToClient->Bind(clientid);
	q_addImageSizeToClient->Write();
	q_addImageSizeToClient->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setImageBackupSynctime
* @sql
*       UPDATE backup_images SET synctime=strftime('%s', 'now') WHERE id=:backupid(int)
*/
void ServerBackupDao::setImageBackupSynctime(int backupid)
{
	if(q_setImageBackupSynctime==NULL)
	{
		q_setImageBackupSynctime=db->Prepare("UPDATE backup_images SET synctime=strftime('%s', 'now') WHERE id=?", false);
	}
	q_setImageBackupSynctime->Bind(backupid);
	q_setImageBackupSynctime->Write();
	q_setImageBackupSynctime->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setImageBackupComplete
* @sql
*       UPDATE backup_images SET complete=1 WHERE id=:backupid(int)
*/
void ServerBackupDao::setImageBackupComplete(int backupid)
{
	if(q_setImageBackupComplete==NULL)
	{
		q_setImageBackupComplete=db->Prepare("UPDATE backup_images SET complete=1 WHERE id=?", false);
	}
	q_setImageBackupComplete->Bind(backupid);
	q_setImageBackupComplete->Write();
	q_setImageBackupComplete->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setImageBackupIncomplete
* @sql
*       UPDATE backup_images SET complete=0 WHERE id=:backupid(int)
*/
void ServerBackupDao::setImageBackupIncomplete(int backupid)
{
	if(q_setImageBackupIncomplete==NULL)
	{
		q_setImageBackupIncomplete=db->Prepare("UPDATE backup_images SET complete=0 WHERE id=?", false);
	}
	q_setImageBackupIncomplete->Bind(backupid);
	q_setImageBackupIncomplete->Write();
	q_setImageBackupIncomplete->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateImageBackupRunning
* @sql
*       UPDATE backup_images SET running=CURRENT_TIMESTAMP WHERE id=:backupid(int)
*/
void ServerBackupDao::updateImageBackupRunning(int backupid)
{
	if(q_updateImageBackupRunning==NULL)
	{
		q_updateImageBackupRunning=db->Prepare("UPDATE backup_images SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	}
	q_updateImageBackupRunning->Bind(backupid);
	q_updateImageBackupRunning->Write();
	q_updateImageBackupRunning->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::saveImageAssociation
* @sql
*       INSERT INTO assoc_images (img_id, assoc_id) VALUES (:img_id(int), :assoc_id(int) )
*/
void ServerBackupDao::saveImageAssociation(int img_id, int assoc_id)
{
	if(q_saveImageAssociation==NULL)
	{
		q_saveImageAssociation=db->Prepare("INSERT INTO assoc_images (img_id, assoc_id) VALUES (?, ? )", false);
	}
	q_saveImageAssociation->Bind(img_id);
	q_saveImageAssociation->Bind(assoc_id);
	q_saveImageAssociation->Write();
	q_saveImageAssociation->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateClientLastImageBackup
* @sql
*       UPDATE clients SET lastbackup_image=(SELECT b.backuptime FROM backup_images b WHERE b.id=:backupid(int)), alerts_next_check=NULL WHERE id=:clientid(int)
*/
void ServerBackupDao::updateClientLastImageBackup(int backupid, int clientid)
{
	if(q_updateClientLastImageBackup==NULL)
	{
		q_updateClientLastImageBackup=db->Prepare("UPDATE clients SET lastbackup_image=(SELECT b.backuptime FROM backup_images b WHERE b.id=?), alerts_next_check=NULL WHERE id=?", false);
	}
	q_updateClientLastImageBackup->Bind(backupid);
	q_updateClientLastImageBackup->Bind(clientid);
	q_updateClientLastImageBackup->Write();
	q_updateClientLastImageBackup->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateClientNumIssues
* @sql
*       UPDATE clients SET last_filebackup_issues=:last_filebackup_issues(int) WHERE id=:clientid(int)
*/
void ServerBackupDao::updateClientNumIssues(int last_filebackup_issues, int clientid)
{
	if(q_updateClientNumIssues==NULL)
	{
		q_updateClientNumIssues=db->Prepare("UPDATE clients SET last_filebackup_issues=? WHERE id=?", false);
	}
	q_updateClientNumIssues->Bind(last_filebackup_issues);
	q_updateClientNumIssues->Bind(clientid);
	q_updateClientNumIssues->Write();
	q_updateClientNumIssues->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateClientLastFileBackup
* @sql
*       UPDATE clients SET lastbackup=(SELECT b.backuptime FROM backups b WHERE b.id=:backupid(int)), last_filebackup_issues=:last_filebackup_issues(int), alerts_next_check=NULL WHERE id=:clientid(int)
*/
void ServerBackupDao::updateClientLastFileBackup(int backupid, int last_filebackup_issues, int clientid)
{
	if(q_updateClientLastFileBackup==NULL)
	{
		q_updateClientLastFileBackup=db->Prepare("UPDATE clients SET lastbackup=(SELECT b.backuptime FROM backups b WHERE b.id=?), last_filebackup_issues=?, alerts_next_check=NULL WHERE id=?", false);
	}
	q_updateClientLastFileBackup->Bind(backupid);
	q_updateClientLastFileBackup->Bind(last_filebackup_issues);
	q_updateClientLastFileBackup->Bind(clientid);
	q_updateClientLastFileBackup->Write();
	q_updateClientLastFileBackup->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateClientOsAndClientVersion
* @sql
*       UPDATE clients SET os_simple=:os_simple(string), os_version_str=:os_version(string), client_version_str=:client_version(string)
*		WHERE id=:clientid(int)
*/
void ServerBackupDao::updateClientOsAndClientVersion(const std::string& os_simple, const std::string& os_version, const std::string& client_version, int clientid)
{
	if(q_updateClientOsAndClientVersion==NULL)
	{
		q_updateClientOsAndClientVersion=db->Prepare("UPDATE clients SET os_simple=?, os_version_str=?, client_version_str=? WHERE id=?", false);
	}
	q_updateClientOsAndClientVersion->Bind(os_simple);
	q_updateClientOsAndClientVersion->Bind(os_version);
	q_updateClientOsAndClientVersion->Bind(client_version);
	q_updateClientOsAndClientVersion->Bind(clientid);
	q_updateClientOsAndClientVersion->Write();
	q_updateClientOsAndClientVersion->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::deleteAllUsersOnClient
* @sql
*       DELETE FROM users_on_client WHERE clientid=:clientid(int)
*/
void ServerBackupDao::deleteAllUsersOnClient(int clientid)
{
	if(q_deleteAllUsersOnClient==NULL)
	{
		q_deleteAllUsersOnClient=db->Prepare("DELETE FROM users_on_client WHERE clientid=?", false);
	}
	q_deleteAllUsersOnClient->Bind(clientid);
	q_deleteAllUsersOnClient->Write();
	q_deleteAllUsersOnClient->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addUserOnClient
* @sql
*       INSERT OR IGNORE INTO users_on_client (clientid, username) VALUES (:clientid(int), :username(string))
*/
void ServerBackupDao::addUserOnClient(int clientid, const std::string& username)
{
	if(q_addUserOnClient==NULL)
	{
		q_addUserOnClient=db->Prepare("INSERT OR IGNORE INTO users_on_client (clientid, username) VALUES (?, ?)", false);
	}
	q_addUserOnClient->Bind(clientid);
	q_addUserOnClient->Bind(username);
	q_addUserOnClient->Write();
	q_addUserOnClient->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addClientToken
* @sql
*       INSERT OR REPLACE INTO tokens_on_client (clientid, token) VALUES (:clientid(int), :token(string))
*/
void ServerBackupDao::addClientToken(int clientid, const std::string& token)
{
	if(q_addClientToken==NULL)
	{
		q_addClientToken=db->Prepare("INSERT OR REPLACE INTO tokens_on_client (clientid, token) VALUES (?, ?)", false);
	}
	q_addClientToken->Bind(clientid);
	q_addClientToken->Bind(token);
	q_addClientToken->Write();
	q_addClientToken->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addUserToken
* @sql
*       INSERT OR REPLACE INTO user_tokens (username, clientid, token) VALUES (:username(string), :clientid(int), :token(string))
*/
void ServerBackupDao::addUserToken(const std::string& username, int clientid, const std::string& token)
{
	if(q_addUserToken==NULL)
	{
		q_addUserToken=db->Prepare("INSERT OR REPLACE INTO user_tokens (username, clientid, token) VALUES (?, ?, ?)", false);
	}
	q_addUserToken->Bind(username);
	q_addUserToken->Bind(clientid);
	q_addUserToken->Bind(token);
	q_addUserToken->Write();
	q_addUserToken->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addUserTokenWithGroup
* @sql
*       INSERT OR REPLACE INTO user_tokens (username, clientid, token, tgroup) VALUES (:username(string), :clientid(int), :token(string), :tgroup(string) )
*/
void ServerBackupDao::addUserTokenWithGroup(const std::string& username, int clientid, const std::string& token, const std::string& tgroup)
{
	if(q_addUserTokenWithGroup==NULL)
	{
		q_addUserTokenWithGroup=db->Prepare("INSERT OR REPLACE INTO user_tokens (username, clientid, token, tgroup) VALUES (?, ?, ?, ? )", false);
	}
	q_addUserTokenWithGroup->Bind(username);
	q_addUserTokenWithGroup->Bind(clientid);
	q_addUserTokenWithGroup->Bind(token);
	q_addUserTokenWithGroup->Bind(tgroup);
	q_addUserTokenWithGroup->Write();
	q_addUserTokenWithGroup->Reset();
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::hasRecentFullOrIncrFileBackup
* @return int64 id
* @sql
*       SELECT id FROM backups
*		WHERE ((datetime('now', :backup_interval_full(string))<backuptime
*				AND clientid=:clientid(int) AND incremental=0)
*		  OR (datetime('now', :backup_interval_incr(string))<backuptime
*				AND clientid=:clientid(int) AND complete=1) )
*		  AND done=1 AND tgroup=:tgroup(int)
*/
ServerBackupDao::CondInt64 ServerBackupDao::hasRecentFullOrIncrFileBackup(const std::string& backup_interval_full, int clientid, const std::string& backup_interval_incr, int tgroup)
{
	if(q_hasRecentFullOrIncrFileBackup==NULL)
	{
		q_hasRecentFullOrIncrFileBackup=db->Prepare("SELECT id FROM backups WHERE ((datetime('now', ?)<backuptime AND clientid=? AND incremental=0) OR (datetime('now', ?)<backuptime AND clientid=? AND complete=1) ) AND done=1 AND tgroup=?", false);
	}
	q_hasRecentFullOrIncrFileBackup->Bind(backup_interval_full);
	q_hasRecentFullOrIncrFileBackup->Bind(clientid);
	q_hasRecentFullOrIncrFileBackup->Bind(backup_interval_incr);
	q_hasRecentFullOrIncrFileBackup->Bind(clientid);
	q_hasRecentFullOrIncrFileBackup->Bind(tgroup);
	db_results res=q_hasRecentFullOrIncrFileBackup->Read();
	q_hasRecentFullOrIncrFileBackup->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::hasRecentIncrFileBackup
* @return int64 id
* @sql
*       SELECT id FROM backups
*		WHERE datetime('now', :backup_interval(string))<backuptime
*			AND clientid=:clientid(int) AND complete=1 AND done=1 AND tgroup=:tgroup(int)
*/
ServerBackupDao::CondInt64 ServerBackupDao::hasRecentIncrFileBackup(const std::string& backup_interval, int clientid, int tgroup)
{
	if(q_hasRecentIncrFileBackup==NULL)
	{
		q_hasRecentIncrFileBackup=db->Prepare("SELECT id FROM backups WHERE datetime('now', ?)<backuptime AND clientid=? AND complete=1 AND done=1 AND tgroup=?", false);
	}
	q_hasRecentIncrFileBackup->Bind(backup_interval);
	q_hasRecentIncrFileBackup->Bind(clientid);
	q_hasRecentIncrFileBackup->Bind(tgroup);
	db_results res=q_hasRecentIncrFileBackup->Read();
	q_hasRecentIncrFileBackup->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::hasRecentFullOrIncrImageBackup
* @return int64 id
* @sql
*       SELECT id FROM backup_images
*		WHERE ( ( datetime('now', :backup_interval_full(string) )<backuptime
*				AND clientid=:clientid(int) AND incremental=0 AND complete=1 )
*			OR
*				( datetime('now', :backup_interval_incr(string) )<backuptime
*				  AND complete=1 )
*           ) AND clientid=:clientid(int) AND version=:image_version(int) AND letter=:letter(string)
*
*/
ServerBackupDao::CondInt64 ServerBackupDao::hasRecentFullOrIncrImageBackup(const std::string& backup_interval_full, int clientid, const std::string& backup_interval_incr, int image_version, const std::string& letter)
{
	if(q_hasRecentFullOrIncrImageBackup==NULL)
	{
		q_hasRecentFullOrIncrImageBackup=db->Prepare("SELECT id FROM backup_images WHERE ( ( datetime('now', ? )<backuptime AND clientid=? AND incremental=0 AND complete=1 ) OR ( datetime('now', ? )<backuptime AND complete=1 ) ) AND clientid=? AND version=? AND letter=?", false);
	}
	q_hasRecentFullOrIncrImageBackup->Bind(backup_interval_full);
	q_hasRecentFullOrIncrImageBackup->Bind(clientid);
	q_hasRecentFullOrIncrImageBackup->Bind(backup_interval_incr);
	q_hasRecentFullOrIncrImageBackup->Bind(clientid);
	q_hasRecentFullOrIncrImageBackup->Bind(image_version);
	q_hasRecentFullOrIncrImageBackup->Bind(letter);
	db_results res=q_hasRecentFullOrIncrImageBackup->Read();
	q_hasRecentFullOrIncrImageBackup->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::hasRecentIncrImageBackup
* @return int64 id
* @sql
*       SELECT id FROM backup_images
*		WHERE datetime('now', :backup_interval(string) )<backuptime
*			AND clientid=:clientid(int) AND complete=1
*           AND version=:image_version(int) AND letter=:letter(string)
*/
ServerBackupDao::CondInt64 ServerBackupDao::hasRecentIncrImageBackup(const std::string& backup_interval, int clientid, int image_version, const std::string& letter)
{
	if(q_hasRecentIncrImageBackup==NULL)
	{
		q_hasRecentIncrImageBackup=db->Prepare("SELECT id FROM backup_images WHERE datetime('now', ? )<backuptime AND clientid=? AND complete=1 AND version=? AND letter=?", false);
	}
	q_hasRecentIncrImageBackup->Bind(backup_interval);
	q_hasRecentIncrImageBackup->Bind(clientid);
	q_hasRecentIncrImageBackup->Bind(image_version);
	q_hasRecentIncrImageBackup->Bind(letter);
	db_results res=q_hasRecentIncrImageBackup->Read();
	q_hasRecentIncrImageBackup->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addRestore
* @sql
*       INSERT INTO restores (clientid, done, path, identity, success, image, letter)
*       VALUES
*			(:clientid(int), 0, :path(string), :identity(string), 0, :image(int), :letter(string) )
*/
void ServerBackupDao::addRestore(int clientid, const std::string& path, const std::string& identity, int image, const std::string& letter)
{
	if(q_addRestore==NULL)
	{
		q_addRestore=db->Prepare("INSERT INTO restores (clientid, done, path, identity, success, image, letter) VALUES (?, 0, ?, ?, 0, ?, ? )", false);
	}
	q_addRestore->Bind(clientid);
	q_addRestore->Bind(path);
	q_addRestore->Bind(identity);
	q_addRestore->Bind(image);
	q_addRestore->Bind(letter);
	q_addRestore->Write();
	q_addRestore->Reset();
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getRestorePath
* @return string path
* @sql
*       SELECT path FROM restores WHERE id=:restore_id(int64) AND clientid=:clientid(int)
*/
ServerBackupDao::CondString ServerBackupDao::getRestorePath(int64 restore_id, int clientid)
{
	if(q_getRestorePath==NULL)
	{
		q_getRestorePath=db->Prepare("SELECT path FROM restores WHERE id=? AND clientid=?", false);
	}
	q_getRestorePath->Bind(restore_id);
	q_getRestorePath->Bind(clientid);
	db_results res=q_getRestorePath->Read();
	q_getRestorePath->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getRestoreIdentity
* @return string identity
* @sql
*       SELECT identity FROM restores WHERE id=:restore_id(int64) AND clientid=:clientid(int)
*/
ServerBackupDao::CondString ServerBackupDao::getRestoreIdentity(int64 restore_id, int clientid)
{
	if(q_getRestoreIdentity==NULL)
	{
		q_getRestoreIdentity=db->Prepare("SELECT identity FROM restores WHERE id=? AND clientid=?", false);
	}
	q_getRestoreIdentity->Bind(restore_id);
	q_getRestoreIdentity->Bind(clientid);
	db_results res=q_getRestoreIdentity->Read();
	q_getRestoreIdentity->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["identity"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setRestoreDone
* @sql
*       UPDATE restores SET done=1, finished=CURRENT_TIMESTAMP, success=:success(int) WHERE id=:restore_id(int64)
*/
void ServerBackupDao::setRestoreDone(int success, int64 restore_id)
{
	if(q_setRestoreDone==NULL)
	{
		q_setRestoreDone=db->Prepare("UPDATE restores SET done=1, finished=CURRENT_TIMESTAMP, success=? WHERE id=?", false);
	}
	q_setRestoreDone->Bind(success);
	q_setRestoreDone->Bind(restore_id);
	q_setRestoreDone->Write();
	q_setRestoreDone->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::deleteRestore
* @sql
*       DELETE FROM restores WHERE id=:restore_id(int64)
*/
void ServerBackupDao::deleteRestore(int64 restore_id)
{
	if(q_deleteRestore==NULL)
	{
		q_deleteRestore=db->Prepare("DELETE FROM restores WHERE id=?", false);
	}
	q_deleteRestore->Bind(restore_id);
	q_deleteRestore->Write();
	q_deleteRestore->Reset();
}

/**
* @-SQLGenAccess
* @func SFileBackupInfo ServerBackupDao::getFileBackupInfo
* @return int64 id, int clientid, int64 backuptime, int incremental, string path, int complete, int64 running, int64 size_bytes, int done, int archived, int64 archive_timeout, int64 size_calculated, int resumed, int64 indexing_time_ms, int tgroup
* @sql
*       SELECT id, clientid, strftime('%s',backuptime) AS backuptime, incremental, path, complete, strftime('%s',running) AS running, size_bytes, done, archived, archive_timeout, size_calculated, resumed, indexing_time_ms, tgroup
*       FROM backups WHERE id=:backupid(int)
*/
ServerBackupDao::SFileBackupInfo ServerBackupDao::getFileBackupInfo(int backupid)
{
	if(q_getFileBackupInfo==NULL)
	{
		q_getFileBackupInfo=db->Prepare("SELECT id, clientid, strftime('%s',backuptime) AS backuptime, incremental, path, complete, strftime('%s',running) AS running, size_bytes, done, archived, archive_timeout, size_calculated, resumed, indexing_time_ms, tgroup FROM backups WHERE id=?", false);
	}
	q_getFileBackupInfo->Bind(backupid);
	db_results res=q_getFileBackupInfo->Read();
	q_getFileBackupInfo->Reset();
	SFileBackupInfo ret = { false, 0, 0, 0, 0, "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.clientid=watoi(res[0]["clientid"]);
		ret.backuptime=watoi64(res[0]["backuptime"]);
		ret.incremental=watoi(res[0]["incremental"]);
		ret.path=res[0]["path"];
		ret.complete=watoi(res[0]["complete"]);
		ret.running=watoi64(res[0]["running"]);
		ret.size_bytes=watoi64(res[0]["size_bytes"]);
		ret.done=watoi(res[0]["done"]);
		ret.archived=watoi(res[0]["archived"]);
		ret.archive_timeout=watoi64(res[0]["archive_timeout"]);
		ret.size_calculated=watoi64(res[0]["size_calculated"]);
		ret.resumed=watoi(res[0]["resumed"]);
		ret.indexing_time_ms=watoi64(res[0]["indexing_time_ms"]);
		ret.tgroup=watoi(res[0]["tgroup"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setVirtualMainClient
* @sql
*       UPDATE clients SET virtualmain=:virtualmain(string) WHERE id=:clientid(int64)
*/
void ServerBackupDao::setVirtualMainClient(const std::string& virtualmain, int64 clientid)
{
	if(q_setVirtualMainClient==NULL)
	{
		q_setVirtualMainClient=db->Prepare("UPDATE clients SET virtualmain=? WHERE id=?", false);
	}
	q_setVirtualMainClient->Bind(virtualmain);
	q_setVirtualMainClient->Bind(clientid);
	q_setVirtualMainClient->Write();
	q_setVirtualMainClient->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::deleteUsedAccessTokens
* @sql
*       DELETE FROM settings_db.access_tokens WHERE clientid=:clientid(int)
*/
void ServerBackupDao::deleteUsedAccessTokens(int clientid)
{
	if(q_deleteUsedAccessTokens==NULL)
	{
		q_deleteUsedAccessTokens=db->Prepare("DELETE FROM settings_db.access_tokens WHERE clientid=?", false);
	}
	q_deleteUsedAccessTokens->Bind(clientid);
	q_deleteUsedAccessTokens->Write();
	q_deleteUsedAccessTokens->Reset();
}

/**
* @-SQLGenAccess
* @func int ServerBackupDao::hasUsedAccessToken
* @return int clientid
* @sql
*       SELECT clientid FROM settings_db.access_tokens WHERE tokenhash=:tokenhash(blob)
*/
ServerBackupDao::CondInt ServerBackupDao::hasUsedAccessToken(const std::string& tokenhash)
{
	if(q_hasUsedAccessToken==NULL)
	{
		q_hasUsedAccessToken=db->Prepare("SELECT clientid FROM settings_db.access_tokens WHERE tokenhash=?", false);
	}
	q_hasUsedAccessToken->Bind(tokenhash.c_str(), (_u32)tokenhash.size());
	db_results res=q_hasUsedAccessToken->Read();
	q_hasUsedAccessToken->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["clientid"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addUsedAccessToken
* @sql
*       INSERT INTO settings_db.access_tokens (clientid, tokenhash)
*		VALUES (:clientid(int), :tokenhash(blob) )
*/
void ServerBackupDao::addUsedAccessToken(int clientid, const std::string& tokenhash)
{
	if(q_addUsedAccessToken==NULL)
	{
		q_addUsedAccessToken=db->Prepare("INSERT INTO settings_db.access_tokens (clientid, tokenhash) VALUES (?, ? )", false);
	}
	q_addUsedAccessToken->Bind(clientid);
	q_addUsedAccessToken->Bind(tokenhash.c_str(), (_u32)tokenhash.size());
	q_addUsedAccessToken->Write();
	q_addUsedAccessToken->Reset();
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getClientnameByImageid
* @return string name
* @sql
*       SELECT name FROM clients WHERE id = (SELECT clientid FROM backup_images WHERE id=:backupid(int) )
*/
ServerBackupDao::CondString ServerBackupDao::getClientnameByImageid(int backupid)
{
	if(q_getClientnameByImageid==NULL)
	{
		q_getClientnameByImageid=db->Prepare("SELECT name FROM clients WHERE id = (SELECT clientid FROM backup_images WHERE id=? )", false);
	}
	q_getClientnameByImageid->Bind(backupid);
	db_results res=q_getClientnameByImageid->Read();
	q_getClientnameByImageid->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["name"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerBackupDao::getClientidByImageid
* @return int clientid
* @sql
*       SELECT clientid FROM backup_images WHERE id=:backupid(int)
*/
ServerBackupDao::CondInt ServerBackupDao::getClientidByImageid(int backupid)
{
	if(q_getClientidByImageid==NULL)
	{
		q_getClientidByImageid=db->Prepare("SELECT clientid FROM backup_images WHERE id=?", false);
	}
	q_getClientidByImageid->Bind(backupid);
	db_results res=q_getClientidByImageid->Read();
	q_getClientidByImageid->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["clientid"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerBackupDao::getImageMounttime
* @return int mounttime
* @sql
*       SELECT mounttime FROM backup_images WHERE id=:backupid(int)
*/
ServerBackupDao::CondInt ServerBackupDao::getImageMounttime(int backupid)
{
	if(q_getImageMounttime==NULL)
	{
		q_getImageMounttime=db->Prepare("SELECT mounttime FROM backup_images WHERE id=?", false);
	}
	q_getImageMounttime->Bind(backupid);
	db_results res=q_getImageMounttime->Read();
	q_getImageMounttime->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["mounttime"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setImageMounted
* @sql
*       UPDATE backup_images SET mounttime=strftime('%s','now') WHERE id=:backupid(int)
*/
void ServerBackupDao::setImageMounted(int backupid)
{
	if(q_setImageMounted==NULL)
	{
		q_setImageMounted=db->Prepare("UPDATE backup_images SET mounttime=strftime('%s','now') WHERE id=?", false);
	}
	q_setImageMounted->Bind(backupid);
	q_setImageMounted->Write();
	q_setImageMounted->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setImageUnmounted
* @sql
*       UPDATE backup_images SET mounttime=0 WHERE id=:backupid(int)
*/
void ServerBackupDao::setImageUnmounted(int backupid)
{
	if(q_setImageUnmounted==NULL)
	{
		q_setImageUnmounted=db->Prepare("UPDATE backup_images SET mounttime=0 WHERE id=?", false);
	}
	q_setImageUnmounted->Bind(backupid);
	q_setImageUnmounted->Write();
	q_setImageUnmounted->Reset();
}

/**
* @-SQLGenAccess
* @func SMountedImage ServerBackupDao::getMountedImage
* @return int id, string path, int64 mounttime
* @sql
*       SELECT id, path, mounttime FROM backup_images WHERE id=:backupid(int)
*/
ServerBackupDao::SMountedImage ServerBackupDao::getMountedImage(int backupid)
{
	if(q_getMountedImage==NULL)
	{
		q_getMountedImage=db->Prepare("SELECT id, path, mounttime FROM backup_images WHERE id=?", false);
	}
	q_getMountedImage->Bind(backupid);
	db_results res=q_getMountedImage->Read();
	q_getMountedImage->Reset();
	SMountedImage ret = { false, 0, "", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0]["id"]);
		ret.path=res[0]["path"];
		ret.mounttime=watoi64(res[0]["mounttime"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SMountedImage> ServerBackupDao::getOldMountedImages
* @return int id, string path, int64 mounttime
* @sql
*       SELECT id, path, mounttime FROM backup_images WHERE mounttime!=0 AND mounttime<(strftime('%s','now')-:times(int64))
*/
std::vector<ServerBackupDao::SMountedImage> ServerBackupDao::getOldMountedImages(int64 times)
{
	if(q_getOldMountedImages==NULL)
	{
		q_getOldMountedImages=db->Prepare("SELECT id, path, mounttime FROM backup_images WHERE mounttime!=0 AND mounttime<(strftime('%s','now')-?)", false);
	}
	q_getOldMountedImages->Bind(times);
	db_results res=q_getOldMountedImages->Read();
	q_getOldMountedImages->Reset();
	std::vector<ServerBackupDao::SMountedImage> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i]["id"]);
		ret[i].path=res[i]["path"];
		ret[i].mounttime=watoi64(res[i]["mounttime"]);
	}
	return ret;
}

//@-SQLGenSetup
void ServerBackupDao::prepareQueries( void )
{
	q_addToOldBackupfolders=NULL;
	q_getOldBackupfolders=NULL;
	q_getDeletePendingClientNames=NULL;
	q_getGroupName=NULL;
	q_getClientGroup=NULL;
	q_getVirtualMainClientname=NULL;
	q_insertIntoOrigClientSettings=NULL;
	q_getOrigClientSettings=NULL;
	q_getLastIncrementalDurations=NULL;
	q_getLastFullDurations=NULL;
	q_getClientSetting=NULL;
	q_getClientIds=NULL;
	q_getClientsByUid=NULL;
	q_deleteClient=NULL;
	q_changeClientName=NULL;
	q_addClientMoved=NULL;
	q_getClientMoved=NULL;
	q_getSetting=NULL;
	q_insertSetting=NULL;
	q_updateSetting=NULL;
	q_getMiscValue=NULL;
	q_addMiscValue=NULL;
	q_delMiscValue=NULL;
	q_setClientUsedFilebackupSize=NULL;
	q_newFileBackup=NULL;
	q_updateFileBackupRunning=NULL;
	q_setFileBackupDone=NULL;
	q_setFileBackupSynced=NULL;
	q_getLastIncrementalFileBackup=NULL;
	q_getLastIncrementalCompleteFileBackup=NULL;
	q_updateFileBackupSetComplete=NULL;
	q_saveBackupLog=NULL;
	q_saveBackupLogData=NULL;
	q_getMailableUserIds=NULL;
	q_getUserRight=NULL;
	q_getUserReportSettings=NULL;
	q_formatUnixtime=NULL;
	q_getLastFullImage=NULL;
	q_getLastImage=NULL;
	q_newImageBackup=NULL;
	q_setImageSize=NULL;
	q_addImageSizeToClient=NULL;
	q_setImageBackupSynctime=NULL;
	q_setImageBackupComplete=NULL;
	q_setImageBackupIncomplete=NULL;
	q_updateImageBackupRunning=NULL;
	q_saveImageAssociation=NULL;
	q_updateClientLastImageBackup=NULL;
	q_updateClientNumIssues=NULL;
	q_updateClientLastFileBackup=NULL;
	q_updateClientOsAndClientVersion=NULL;
	q_deleteAllUsersOnClient=NULL;
	q_addUserOnClient=NULL;
	q_addClientToken=NULL;
	q_addUserToken=NULL;
	q_addUserTokenWithGroup=NULL;
	q_hasRecentFullOrIncrFileBackup=NULL;
	q_hasRecentIncrFileBackup=NULL;
	q_hasRecentFullOrIncrImageBackup=NULL;
	q_hasRecentIncrImageBackup=NULL;
	q_addRestore=NULL;
	q_getRestorePath=NULL;
	q_getRestoreIdentity=NULL;
	q_setRestoreDone=NULL;
	q_deleteRestore=NULL;
	q_getFileBackupInfo=NULL;
	q_setVirtualMainClient=NULL;
	q_deleteUsedAccessTokens=NULL;
	q_hasUsedAccessToken=NULL;
	q_addUsedAccessToken=NULL;
	q_getClientnameByImageid=NULL;
	q_getClientidByImageid=NULL;
	q_getImageMounttime=NULL;
	q_setImageMounted=NULL;
	q_setImageUnmounted=NULL;
	q_getMountedImage=NULL;
	q_getOldMountedImages=NULL;
}

//@-SQLGenDestruction
void ServerBackupDao::destroyQueries( void )
{
	db->destroyQuery(q_addToOldBackupfolders);
	db->destroyQuery(q_getOldBackupfolders);
	db->destroyQuery(q_getDeletePendingClientNames);
	db->destroyQuery(q_getGroupName);
	db->destroyQuery(q_getClientGroup);
	db->destroyQuery(q_getVirtualMainClientname);
	db->destroyQuery(q_insertIntoOrigClientSettings);
	db->destroyQuery(q_getOrigClientSettings);
	db->destroyQuery(q_getLastIncrementalDurations);
	db->destroyQuery(q_getLastFullDurations);
	db->destroyQuery(q_getClientSetting);
	db->destroyQuery(q_getClientIds);
	db->destroyQuery(q_getClientsByUid);
	db->destroyQuery(q_deleteClient);
	db->destroyQuery(q_changeClientName);
	db->destroyQuery(q_addClientMoved);
	db->destroyQuery(q_getClientMoved);
	db->destroyQuery(q_getSetting);
	db->destroyQuery(q_insertSetting);
	db->destroyQuery(q_updateSetting);
	db->destroyQuery(q_getMiscValue);
	db->destroyQuery(q_addMiscValue);
	db->destroyQuery(q_delMiscValue);
	db->destroyQuery(q_setClientUsedFilebackupSize);
	db->destroyQuery(q_newFileBackup);
	db->destroyQuery(q_updateFileBackupRunning);
	db->destroyQuery(q_setFileBackupDone);
	db->destroyQuery(q_setFileBackupSynced);
	db->destroyQuery(q_getLastIncrementalFileBackup);
	db->destroyQuery(q_getLastIncrementalCompleteFileBackup);
	db->destroyQuery(q_updateFileBackupSetComplete);
	db->destroyQuery(q_saveBackupLog);
	db->destroyQuery(q_saveBackupLogData);
	db->destroyQuery(q_getMailableUserIds);
	db->destroyQuery(q_getUserRight);
	db->destroyQuery(q_getUserReportSettings);
	db->destroyQuery(q_formatUnixtime);
	db->destroyQuery(q_getLastFullImage);
	db->destroyQuery(q_getLastImage);
	db->destroyQuery(q_newImageBackup);
	db->destroyQuery(q_setImageSize);
	db->destroyQuery(q_addImageSizeToClient);
	db->destroyQuery(q_setImageBackupSynctime);
	db->destroyQuery(q_setImageBackupComplete);
	db->destroyQuery(q_setImageBackupIncomplete);
	db->destroyQuery(q_updateImageBackupRunning);
	db->destroyQuery(q_saveImageAssociation);
	db->destroyQuery(q_updateClientLastImageBackup);
	db->destroyQuery(q_updateClientNumIssues);
	db->destroyQuery(q_updateClientLastFileBackup);
	db->destroyQuery(q_updateClientOsAndClientVersion);
	db->destroyQuery(q_deleteAllUsersOnClient);
	db->destroyQuery(q_addUserOnClient);
	db->destroyQuery(q_addClientToken);
	db->destroyQuery(q_addUserToken);
	db->destroyQuery(q_addUserTokenWithGroup);
	db->destroyQuery(q_hasRecentFullOrIncrFileBackup);
	db->destroyQuery(q_hasRecentIncrFileBackup);
	db->destroyQuery(q_hasRecentFullOrIncrImageBackup);
	db->destroyQuery(q_hasRecentIncrImageBackup);
	db->destroyQuery(q_addRestore);
	db->destroyQuery(q_getRestorePath);
	db->destroyQuery(q_getRestoreIdentity);
	db->destroyQuery(q_setRestoreDone);
	db->destroyQuery(q_deleteRestore);
	db->destroyQuery(q_getFileBackupInfo);
	db->destroyQuery(q_setVirtualMainClient);
	db->destroyQuery(q_deleteUsedAccessTokens);
	db->destroyQuery(q_hasUsedAccessToken);
	db->destroyQuery(q_addUsedAccessToken);
	db->destroyQuery(q_getClientnameByImageid);
	db->destroyQuery(q_getClientidByImageid);
	db->destroyQuery(q_getImageMounttime);
	db->destroyQuery(q_setImageMounted);
	db->destroyQuery(q_setImageUnmounted);
	db->destroyQuery(q_getMountedImage);
	db->destroyQuery(q_getOldMountedImages);
}


void ServerBackupDao::updateOrInsertSetting( int clientid, const std::string& key, const std::string& value )
{
	if(getSetting(clientid, key).exists)
	{
		updateSetting(value, key, clientid);
	}
	else
	{
		insertSetting(key, value, clientid);
	}
}
