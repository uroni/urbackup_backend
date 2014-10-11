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

#include "ServerBackupDao.h"
#include "../../stringtools.h"
#include <assert.h>
#include <string.h>

const int ServerBackupDao::c_direction_incoming = 0;
const int ServerBackupDao::c_direction_outgoing = 1;

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TEMPORARY TABLE files_last ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, rsize INTEGER);
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
* @func void ServerBackupDao::addDirectoryLink
* @sql
*	INSERT INTO directory_links 
*		(clientid, name, target)
*	VALUES
*		(:clientid(int),
*		 :name(string),
*		 :target(string))
*/
void ServerBackupDao::addDirectoryLink(int clientid, const std::wstring& name, const std::wstring& target)
{
	if(q_addDirectoryLink==NULL)
	{
		q_addDirectoryLink=db->Prepare("INSERT INTO directory_links  (clientid, name, target) VALUES (?, ?, ?)", false);
	}
	q_addDirectoryLink->Bind(clientid);
	q_addDirectoryLink->Bind(name);
	q_addDirectoryLink->Bind(target);
	q_addDirectoryLink->Write();
	q_addDirectoryLink->Reset();
}


/**
* @-SQLGenAccess
* @func void ServerBackupDao::removeDirectoryLink
* @sql
*     DELETE FROM directory_links
*          WHERE clientid=:clientid(int)
*			   AND target=:target(string)
*/
void ServerBackupDao::removeDirectoryLink(int clientid, const std::wstring& target)
{
	if(q_removeDirectoryLink==NULL)
	{
		q_removeDirectoryLink=db->Prepare("DELETE FROM directory_links WHERE clientid=? AND target=?", false);
	}
	q_removeDirectoryLink->Bind(clientid);
	q_removeDirectoryLink->Bind(target);
	q_removeDirectoryLink->Write();
	q_removeDirectoryLink->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::removeDirectoryLinkGlob
* @sql
*     DELETE FROM directory_links
*          WHERE clientid=:clientid(int)
*			   AND target GLOB :target(string)
*/
void ServerBackupDao::removeDirectoryLinkGlob(int clientid, const std::wstring& target)
{
	if(q_removeDirectoryLinkGlob==NULL)
	{
		q_removeDirectoryLinkGlob=db->Prepare("DELETE FROM directory_links WHERE clientid=? AND target GLOB ?", false);
	}
	q_removeDirectoryLinkGlob->Bind(clientid);
	q_removeDirectoryLinkGlob->Bind(target);
	q_removeDirectoryLinkGlob->Write();
	q_removeDirectoryLinkGlob->Reset();
}

/**
* @-SQLGenAccess
* @func int ServerBackupDao::getDirectoryRefcount
* @return int_raw c
* @sql
*    SELECT COUNT(*) AS c FROM directory_links
*        WHERE clientid=:clientid(int)
*              AND name=:name(string)
*        LIMIT 1
*/
int ServerBackupDao::getDirectoryRefcount(int clientid, const std::wstring& name)
{
	if(q_getDirectoryRefcount==NULL)
	{
		q_getDirectoryRefcount=db->Prepare("SELECT COUNT(*) AS c FROM directory_links WHERE clientid=? AND name=? LIMIT 1", false);
	}
	q_getDirectoryRefcount->Bind(clientid);
	q_getDirectoryRefcount->Bind(name);
	db_results res=q_getDirectoryRefcount->Read();
	q_getDirectoryRefcount->Reset();
	assert(!res.empty());
	return watoi(res[0][L"c"]);
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addDirectoryLinkJournalEntry
* @sql
*    INSERT INTO directory_link_journal (linkname, linktarget)
*     VALUES (:linkname(string), :linktarget(string))
*/
void ServerBackupDao::addDirectoryLinkJournalEntry(const std::wstring& linkname, const std::wstring& linktarget)
{
	if(q_addDirectoryLinkJournalEntry==NULL)
	{
		q_addDirectoryLinkJournalEntry=db->Prepare("INSERT INTO directory_link_journal (linkname, linktarget) VALUES (?, ?)", false);
	}
	q_addDirectoryLinkJournalEntry->Bind(linkname);
	q_addDirectoryLinkJournalEntry->Bind(linktarget);
	q_addDirectoryLinkJournalEntry->Write();
	q_addDirectoryLinkJournalEntry->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::removeDirectoryLinkJournalEntry
* @sql
*     DELETE FROM directory_link_journal WHERE
*             id = :entry_id(int64)
*/
void ServerBackupDao::removeDirectoryLinkJournalEntry(int64 entry_id)
{
	if(q_removeDirectoryLinkJournalEntry==NULL)
	{
		q_removeDirectoryLinkJournalEntry=db->Prepare("DELETE FROM directory_link_journal WHERE id = ?", false);
	}
	q_removeDirectoryLinkJournalEntry->Bind(entry_id);
	q_removeDirectoryLinkJournalEntry->Write();
	q_removeDirectoryLinkJournalEntry->Reset();
}

/**
* @-SQLGenAccess
* @func vector<JournalEntry> ServerBackupDao::getDirectoryLinkJournalEntries
* @return string linkname, string linktarget
* @sql
*     SELECT linkname, linktarget FROM directory_link_journal
*/
std::vector<ServerBackupDao::JournalEntry> ServerBackupDao::getDirectoryLinkJournalEntries(void)
{
	if(q_getDirectoryLinkJournalEntries==NULL)
	{
		q_getDirectoryLinkJournalEntries=db->Prepare("SELECT linkname, linktarget FROM directory_link_journal", false);
	}
	db_results res=q_getDirectoryLinkJournalEntries->Read();
	std::vector<ServerBackupDao::JournalEntry> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].linkname=res[i][L"linkname"];
		ret[i].linktarget=res[i][L"linktarget"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::removeDirectoryLinkJournalEntries
* @sql
*     DELETE FROM directory_link_journal
*/
void ServerBackupDao::removeDirectoryLinkJournalEntries(void)
{
	if(q_removeDirectoryLinkJournalEntries==NULL)
	{
		q_removeDirectoryLinkJournalEntries=db->Prepare("DELETE FROM directory_link_journal", false);
	}
	q_removeDirectoryLinkJournalEntries->Write();
}

/**
* @-SQLGenAccess
* @func vector<DirectoryLinkEntry> ServerBackupDao::getLinksInDirectory
* @return string name, string target
* @sql
*     SELECT name, target FROM directory_links
*            WHERE clientid=:clientid(int) AND
*                  target GLOB :dir(string)
*/
std::vector<ServerBackupDao::DirectoryLinkEntry> ServerBackupDao::getLinksInDirectory(int clientid, const std::wstring& dir)
{
	if(q_getLinksInDirectory==NULL)
	{
		q_getLinksInDirectory=db->Prepare("SELECT name, target FROM directory_links WHERE clientid=? AND target GLOB ?", false);
	}
	q_getLinksInDirectory->Bind(clientid);
	q_getLinksInDirectory->Bind(dir);
	db_results res=q_getLinksInDirectory->Read();
	q_getLinksInDirectory->Reset();
	std::vector<ServerBackupDao::DirectoryLinkEntry> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].name=res[i][L"name"];
		ret[i].target=res[i][L"target"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::deleteLinkReferenceEntry
* @sql
*     DELETE FROM directory_links
*            WHERE id=:id(int64)
*/
void ServerBackupDao::deleteLinkReferenceEntry(int64 id)
{
	if(q_deleteLinkReferenceEntry==NULL)
	{
		q_deleteLinkReferenceEntry=db->Prepare("DELETE FROM directory_links WHERE id=?", false);
	}
	q_deleteLinkReferenceEntry->Bind(id);
	q_deleteLinkReferenceEntry->Write();
	q_deleteLinkReferenceEntry->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateLinkReferenceTarget
* @sql
*     UPDATE directory_links SET target=:new_target(string)
*            WHERE id=:id(int64)
*/
void ServerBackupDao::updateLinkReferenceTarget(const std::wstring& new_target, int64 id)
{
	if(q_updateLinkReferenceTarget==NULL)
	{
		q_updateLinkReferenceTarget=db->Prepare("UPDATE directory_links SET target=? WHERE id=?", false);
	}
	q_updateLinkReferenceTarget->Bind(new_target);
	q_updateLinkReferenceTarget->Bind(id);
	q_updateLinkReferenceTarget->Write();
	q_updateLinkReferenceTarget->Reset();
}


/**
* @-SQLGenAccess
* @func void ServerBackupDao::addToOldBackupfolders
* @sql
*      INSERT OR REPLACE INTO settings_db.old_backupfolders (backupfolder)
*          VALUES (:backupfolder(string))
*/
void ServerBackupDao::addToOldBackupfolders(const std::wstring& backupfolder)
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
std::vector<std::wstring> ServerBackupDao::getOldBackupfolders(void)
{
	if(q_getOldBackupfolders==NULL)
	{
		q_getOldBackupfolders=db->Prepare("SELECT backupfolder FROM settings_db.old_backupfolders", false);
	}
	db_results res=q_getOldBackupfolders->Read();
	std::vector<std::wstring> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i][L"backupfolder"];
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
std::vector<std::wstring> ServerBackupDao::getDeletePendingClientNames(void)
{
	if(q_getDeletePendingClientNames==NULL)
	{
		q_getDeletePendingClientNames=db->Prepare("SELECT name FROM clients WHERE delete_pending=1", false);
	}
	db_results res=q_getDeletePendingClientNames->Read();
	std::vector<std::wstring> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i][L"name"];
	}
	return ret;
}

/**
* @-SQLGenAccessNoCheck
* @func bool ServerBackupDao::createTemporaryLastFilesTable
* @sql
*      CREATE TEMPORARY TABLE files_last ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE, rsize INTEGER );
*/
bool ServerBackupDao::createTemporaryLastFilesTable(void)
{
	if(q_createTemporaryLastFilesTable==NULL)
	{
		q_createTemporaryLastFilesTable=db->Prepare("CREATE TEMPORARY TABLE files_last ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE, rsize INTEGER );", false);
	}
	bool ret = q_createTemporaryLastFilesTable->Write();
	return ret;
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerBackupDao::dropTemporaryLastFilesTable
* @sql
*      DROP TABLE files_last
*/
void ServerBackupDao::dropTemporaryLastFilesTable(void)
{
	if(q_dropTemporaryLastFilesTable==NULL)
	{
		q_dropTemporaryLastFilesTable=db->Prepare("DROP TABLE files_last", false);
	}
	q_dropTemporaryLastFilesTable->Write();
}

/**
* @-SQLGenAccessNoCheck
* @func bool ServerBackupDao::createTemporaryLastFilesTableIndex
* @sql
*      CREATE INDEX files_last_idx ON files_last ( fullpath );
*/
bool ServerBackupDao::createTemporaryLastFilesTableIndex(void)
{
	if(q_createTemporaryLastFilesTableIndex==NULL)
	{
		q_createTemporaryLastFilesTableIndex=db->Prepare("CREATE INDEX files_last_idx ON files_last ( fullpath );", false);
	}
	bool ret = q_createTemporaryLastFilesTableIndex->Write();
	return ret;
}

/**
* @-SQLGenAccessNoCheck
* @func bool ServerBackupDao::dropTemporaryLastFilesTableIndex
* @sql
*      DROP INDEX files_last_idx;
*/
bool ServerBackupDao::dropTemporaryLastFilesTableIndex(void)
{
	if(q_dropTemporaryLastFilesTableIndex==NULL)
	{
		q_dropTemporaryLastFilesTableIndex=db->Prepare("DROP INDEX files_last_idx;", false);
	}
	bool ret = q_dropTemporaryLastFilesTableIndex->Write();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool ServerBackupDao::copyToTemporaryLastFilesTable
* @sql
*      INSERT INTO files_last (fullpath, hashpath, shahash, filesize, rsize)
*			SELECT fullpath, hashpath, shahash, filesize, rsize FROM files
*				WHERE backupid = :backupid(int)
*/
bool ServerBackupDao::copyToTemporaryLastFilesTable(int backupid)
{
	if(q_copyToTemporaryLastFilesTable==NULL)
	{
		q_copyToTemporaryLastFilesTable=db->Prepare("INSERT INTO files_last (fullpath, hashpath, shahash, filesize, rsize) SELECT fullpath, hashpath, shahash, filesize, rsize FROM files WHERE backupid = ?", false);
	}
	q_copyToTemporaryLastFilesTable->Bind(backupid);
	bool ret = q_copyToTemporaryLastFilesTable->Write();
	q_copyToTemporaryLastFilesTable->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func SFileEntry ServerBackupDao::getFileEntryFromTemporaryTable
* @return string fullpath, string hashpath, blob shahash, int64 filesize, int64 rsize
* @sql
*      SELECT fullpath, hashpath, shahash, filesize, rsize
*       FROM files_last WHERE fullpath = :fullpath(string)
*/
ServerBackupDao::SFileEntry ServerBackupDao::getFileEntryFromTemporaryTable(const std::wstring& fullpath)
{
	if(q_getFileEntryFromTemporaryTable==NULL)
	{
		q_getFileEntryFromTemporaryTable=db->Prepare("SELECT fullpath, hashpath, shahash, filesize, rsize FROM files_last WHERE fullpath = ?", false);
	}
	q_getFileEntryFromTemporaryTable->Bind(fullpath);
	db_results res=q_getFileEntryFromTemporaryTable->Read();
	q_getFileEntryFromTemporaryTable->Reset();
	SFileEntry ret = { false, L"", L"", "", 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.fullpath=res[0][L"fullpath"];
		ret.hashpath=res[0][L"hashpath"];
		std::wstring& val1 = res[0][L"shahash"];
		ret.shahash.resize(val1.size()*sizeof(wchar_t));
		memcpy(&ret.shahash[0], val1.data(), val1.size()*sizeof(wchar_t));
		ret.filesize=watoi64(res[0][L"filesize"]);
		ret.rsize=watoi64(res[0][L"rsize"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SFileEntry> ServerBackupDao::getFileEntriesFromTemporaryTableGlob
* @return string fullpath, string hashpath, blob shahash, int64 filesize
* @sql
*      SELECT fullpath, hashpath, shahash, filesize, rsize
*       FROM files_last WHERE fullpath GLOB :fullpath_glob(string)
*/
std::vector<ServerBackupDao::SFileEntry> ServerBackupDao::getFileEntriesFromTemporaryTableGlob(const std::wstring& fullpath_glob)
{
	if(q_getFileEntriesFromTemporaryTableGlob==NULL)
	{
		q_getFileEntriesFromTemporaryTableGlob=db->Prepare("SELECT fullpath, hashpath, shahash, filesize, rsize FROM files_last WHERE fullpath GLOB ?", false);
	}
	q_getFileEntriesFromTemporaryTableGlob->Bind(fullpath_glob);
	db_results res=q_getFileEntriesFromTemporaryTableGlob->Read();
	q_getFileEntriesFromTemporaryTableGlob->Reset();
	std::vector<ServerBackupDao::SFileEntry> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].fullpath=res[i][L"fullpath"];
		ret[i].hashpath=res[i][L"hashpath"];
		std::wstring& val2 = res[i][L"shahash"];
		ret[i].shahash.resize(val2.size()*sizeof(wchar_t));
		memcpy(&ret[i].shahash[0], val2.data(), val2.size()*sizeof(wchar_t));
		ret[i].filesize=watoi64(res[i][L"filesize"]);
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
void ServerBackupDao::insertIntoOrigClientSettings(int clientid, std::string data)
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
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"data"];
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
		ret[i].indexing_time_ms=watoi64(res[i][L"indexing_time_ms"]);
		ret[i].duration=watoi64(res[i][L"duration"]);
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
		ret[i].indexing_time_ms=watoi64(res[i][L"indexing_time_ms"]);
		ret[i].duration=watoi64(res[i][L"duration"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setNextEntry
* @sql
*      UPDATE files SET next_entry=:next_entry(int64) WHERE id=:id(int64)
*/
void ServerBackupDao::setNextEntry(int64 next_entry, int64 id)
{
	if(q_setNextEntry==NULL)
	{
		q_setNextEntry=db->Prepare("UPDATE files SET next_entry=? WHERE id=?", false);
	}
	q_setNextEntry->Bind(next_entry);
	q_setNextEntry->Bind(id);
	q_setNextEntry->Write();
	q_setNextEntry->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setPrevEntry
* @sql
*      UPDATE files SET prev_entry=:prev_entry(int64) WHERE id=:id(int64)
*/
void ServerBackupDao::setPrevEntry(int64 prev_entry, int64 id)
{
	if(q_setPrevEntry==NULL)
	{
		q_setPrevEntry=db->Prepare("UPDATE files SET prev_entry=? WHERE id=?", false);
	}
	q_setPrevEntry->Bind(prev_entry);
	q_setPrevEntry->Bind(id);
	q_setPrevEntry->Write();
	q_setPrevEntry->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::setPointedTo
* @sql
*      UPDATE files SET pointed_to=:pointed_to(int64) WHERE id=:id(int64)
*/
void ServerBackupDao::setPointedTo(int64 pointed_to, int64 id)
{
	if(q_setPointedTo==NULL)
	{
		q_setPointedTo=db->Prepare("UPDATE files SET pointed_to=? WHERE id=?", false);
	}
	q_setPointedTo->Bind(pointed_to);
	q_setPointedTo->Bind(id);
	q_setPointedTo->Write();
	q_setPointedTo->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addFileEntry
* @sql
*	   INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, pointed_to)
*      VALUES (:backupid(int), :fullpath(string), :hashpath(string), :shahash(blob),
*				:filesize(int64), :rsize(int64), :clientid(int), :incremental(int), :next_entry(int64), :prev_entry(int64), :pointed_to(int))
*/
void ServerBackupDao::addFileEntry(int backupid, const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to)
{

* @func string ServerBackupDao::getSetting
* @return string value
* @sql
*      SELECT value FROM settings_db.settings WHERE clientid=:clientid(int) AND key=:key(string)
*/
ServerBackupDao::CondString ServerBackupDao::getSetting(int clientid, const std::wstring& key)
{
	if(q_getSetting==NULL)
	{
		q_getSetting=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=? AND key=?", false);
	}
	q_getSetting->Bind(clientid);
	q_getSetting->Bind(key);
	db_results res=q_getSetting->Read();
	q_getSetting->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"value"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::insertSetting
* @sql
*      INSERT INTO settings_db.settings (key, value, clientid) VALUES ( :key(string), :value(string), :clientid(int) )
*/
void ServerBackupDao::insertSetting(const std::wstring& key, const std::wstring& value, int clientid)
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
void ServerBackupDao::updateSetting(const std::wstring& value, const std::wstring& key, int clientid)
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

	if(q_addFileEntry==NULL)
	{
		q_addFileEntry=db->Prepare("INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, pointed_to) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", false);
	}
	q_addFileEntry->Bind(backupid);
	q_addFileEntry->Bind(fullpath);
	q_addFileEntry->Bind(hashpath);
	q_addFileEntry->Bind(shahash.c_str(), (_u32)shahash.size());
	q_addFileEntry->Bind(filesize);
	q_addFileEntry->Bind(rsize);
	q_addFileEntry->Bind(clientid);
	q_addFileEntry->Bind(incremental);
	q_addFileEntry->Bind(next_entry);
	q_addFileEntry->Bind(prev_entry);
	q_addFileEntry->Bind(pointed_to);
	q_addFileEntry->Write();
	q_addFileEntry->Reset();
}


/**
* @-SQLGenAccess
* @func void ServerBackupDao::delFileEntry
* @sql
*	   DELETE FROM files WHERE id=:id(int64)
*/
void ServerBackupDao::delFileEntry(int64 id)
{
	if(q_delFileEntry==NULL)
	{
		q_delFileEntry=db->Prepare("DELETE FROM files WHERE id=?", false);
	}
	q_delFileEntry->Bind(id);
	q_delFileEntry->Write();
	q_delFileEntry->Reset();
}

/**
* @-SQLGenAccess
* @func SFindFileEntry ServerBackupDao::getFileEntry
* @return int64 id, string shahash, int backupid, int clientid, string fullpath, string hashpath, int64 filesize, int64 next_entry, int64 prev_entry, int64 rsize, int incremental, int pointed_to
* @sql
*	   SELECT id, shahash, backupid, clientid, fullpath, hashpath, filesize, next_entry, prev_entry, rsize, incremental, pointed_to
*      FROM files WHERE id=:id(int64)
*/
ServerBackupDao::SFindFileEntry ServerBackupDao::getFileEntry(int64 id)
{
	if(q_getFileEntry==NULL)
	{
		q_getFileEntry=db->Prepare("SELECT id, shahash, backupid, clientid, fullpath, hashpath, filesize, next_entry, prev_entry, rsize, incremental, pointed_to FROM files WHERE id=?", false);
	}
	q_getFileEntry->Bind(id);
	db_results res=q_getFileEntry->Read();
	q_getFileEntry->Reset();
	SFindFileEntry ret = { false, 0, L"", 0, 0, L"", L"", 0, 0, 0, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0][L"id"]);
		ret.shahash=res[0][L"shahash"];
		ret.backupid=watoi(res[0][L"backupid"]);
		ret.clientid=watoi(res[0][L"clientid"]);
		ret.fullpath=res[0][L"fullpath"];
		ret.hashpath=res[0][L"hashpath"];
		ret.filesize=watoi64(res[0][L"filesize"]);
		ret.next_entry=watoi64(res[0][L"next_entry"]);
		ret.prev_entry=watoi64(res[0][L"prev_entry"]);
		ret.rsize=watoi64(res[0][L"rsize"]);
		ret.incremental=watoi(res[0][L"incremental"]);
		ret.pointed_to=watoi(res[0][L"pointed_to"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SStatFileEntry ServerBackupDao::getStatFileEntry
* @return int64 id, int backupid, int clientid, int64 filesize, int64 rsize, string shahash, int64 next_entry, int64 prev_entry
* @sql
*	   SELECT id, backupid, clientid, filesize, rsize, shahash, next_entry, prev_entry
*      FROM files WHERE id=:id(int64)
*/
ServerBackupDao::SStatFileEntry ServerBackupDao::getStatFileEntry(int64 id)
{
	if(q_getStatFileEntry==NULL)
	{
		q_getStatFileEntry=db->Prepare("SELECT id, backupid, clientid, filesize, rsize, shahash, next_entry, prev_entry FROM files WHERE id=?", false);
	}
	q_getStatFileEntry->Bind(id);
	db_results res=q_getStatFileEntry->Read();
	q_getStatFileEntry->Reset();
	SStatFileEntry ret = { false, 0, 0, 0, 0, 0, L"", 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0][L"id"]);
		ret.backupid=watoi(res[0][L"backupid"]);
		ret.clientid=watoi(res[0][L"clientid"]);
		ret.filesize=watoi64(res[0][L"filesize"]);
		ret.rsize=watoi64(res[0][L"rsize"]);
		ret.shahash=res[0][L"shahash"];
		ret.next_entry=watoi64(res[0][L"next_entry"]);
		ret.prev_entry=watoi64(res[0][L"prev_entry"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addIncomingFile
* @sql
*       INSERT INTO files_incoming_stat (filesize, clientid, backupid, existing_clients, direction, incremental)
*       VALUES (:filesize(int64), :clientid(int), :backupid(int), :existing_clients(string), :direction(int), :incremental(int))
*/
void ServerBackupDao::addIncomingFile(int64 filesize, int clientid, int backupid, const std::wstring& existing_clients, int direction, int incremental)
{
	if(q_addIncomingFile==NULL)
	{
		q_addIncomingFile=db->Prepare("INSERT INTO files_incoming_stat (filesize, clientid, backupid, existing_clients, direction, incremental) VALUES (?, ?, ?, ?, ?, ?)", false);
	}
	q_addIncomingFile->Bind(filesize);
	q_addIncomingFile->Bind(clientid);
	q_addIncomingFile->Bind(backupid);
	q_addIncomingFile->Bind(existing_clients);
	q_addIncomingFile->Bind(direction);
	q_addIncomingFile->Bind(incremental);
	q_addIncomingFile->Write();
	q_addIncomingFile->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SIncomingStat> ServerBackupDao::getIncomingStats
* @return int64 id, int64 filesize, int clientid, int backupid, string existing_clients, int direction, int incremental
* @sql
*       SELECT id, filesize, clientid, backupid, existing_clients, direction, incremental
*       FROM files_incoming_stat LIMIT 10000
*/
std::vector<ServerBackupDao::SIncomingStat> ServerBackupDao::getIncomingStats(void)
{
	if(q_getIncomingStats==NULL)
	{
		q_getIncomingStats=db->Prepare("SELECT id, filesize, clientid, backupid, existing_clients, direction, incremental FROM files_incoming_stat LIMIT 10000", false);
	}
	db_results res=q_getIncomingStats->Read();
	std::vector<ServerBackupDao::SIncomingStat> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i][L"id"]);
		ret[i].filesize=watoi64(res[i][L"filesize"]);
		ret[i].clientid=watoi(res[i][L"clientid"]);
		ret[i].backupid=watoi(res[i][L"backupid"]);
		ret[i].existing_clients=res[i][L"existing_clients"];
		ret[i].direction=watoi(res[i][L"direction"]);
		ret[i].incremental=watoi(res[i][L"incremental"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::getIncomingStatsCount
* @return int64 c
* @sql
*       SELECT COUNT(*) AS c
*       FROM files_incoming_stat
*/
ServerBackupDao::CondInt64 ServerBackupDao::getIncomingStatsCount(void)
{
	if(q_getIncomingStatsCount==NULL)
	{
		q_getIncomingStatsCount=db->Prepare("SELECT COUNT(*) AS c FROM files_incoming_stat", false);
	}
	db_results res=q_getIncomingStatsCount->Read();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"c"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::delIncomingStatEntry
* @sql
*       DELETE FROM files_incoming_stat WHERE id=:id(int64)
*/
void ServerBackupDao::delIncomingStatEntry(int64 id)
{
	if(q_delIncomingStatEntry==NULL)
	{
		q_delIncomingStatEntry=db->Prepare("DELETE FROM files_incoming_stat WHERE id=?", false);
	}
	q_delIncomingStatEntry->Bind(id);
	q_delIncomingStatEntry->Write();
	q_delIncomingStatEntry->Reset();
}

/**
* @-SQLGenAccess
* @func string ServerBackupDao::getMiscValue
* @return string tvalue
* @sql
*       SELECT tvalue FROM misc WHERE tkey=:tkey(string)
*/
ServerBackupDao::CondString ServerBackupDao::getMiscValue(const std::wstring& tkey)
{
	if(q_getMiscValue==NULL)
	{
		q_getMiscValue=db->Prepare("SELECT tvalue FROM misc WHERE tkey=?", false);
	}
	q_getMiscValue->Bind(tkey);
	db_results res=q_getMiscValue->Read();
	q_getMiscValue->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"tvalue"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::addMiscValue
* @sql
*       INSERT INTO misc (tkey, tvalue) VALUES (:tkey(string), :tvalue(string))
*/
void ServerBackupDao::addMiscValue(const std::wstring& tkey, const std::wstring& tvalue)
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
void ServerBackupDao::delMiscValue(const std::wstring& tkey)
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



//@-SQLGenSetup
void ServerBackupDao::prepareQueries( void )
{
	q_addDirectoryLink=NULL;
	q_removeDirectoryLink=NULL;
	q_removeDirectoryLinkGlob=NULL;
	q_getDirectoryRefcount=NULL;
	q_addDirectoryLinkJournalEntry=NULL;
	q_removeDirectoryLinkJournalEntry=NULL;
	q_getDirectoryLinkJournalEntries=NULL;
	q_removeDirectoryLinkJournalEntries=NULL;
	q_getLinksInDirectory=NULL;
	q_deleteLinkReferenceEntry=NULL;
	q_updateLinkReferenceTarget=NULL;
	q_addToOldBackupfolders=NULL;
	q_getOldBackupfolders=NULL;
	q_getDeletePendingClientNames=NULL;
	q_createTemporaryLastFilesTable=NULL;
	q_dropTemporaryLastFilesTable=NULL;
	q_createTemporaryLastFilesTableIndex=NULL;
	q_dropTemporaryLastFilesTableIndex=NULL;
	q_copyToTemporaryLastFilesTable=NULL;
	q_getFileEntryFromTemporaryTable=NULL;
	q_getFileEntriesFromTemporaryTableGlob=NULL;
	q_insertIntoOrigClientSettings=NULL;
	q_getOrigClientSettings=NULL;
	q_getLastIncrementalDurations=NULL;
	q_getLastFullDurations=NULL;
	q_setNextEntry=NULL;
	q_setPrevEntry=NULL;
	q_setPointedTo=NULL;
	q_addFileEntry=NULL;
	q_delFileEntry=NULL;
	q_getFileEntry=NULL;
	q_getStatFileEntry=NULL;
	q_addIncomingFile=NULL;
	q_getIncomingStats=NULL;
	q_getIncomingStatsCount=NULL;
	q_delIncomingStatEntry=NULL;
	q_getMiscValue=NULL;
	q_addMiscValue=NULL;
	q_delMiscValue=NULL;
	q_setClientUsedFilebackupSize=NULL;
}

//@-SQLGenDestruction
void ServerBackupDao::destroyQueries( void )
{
	db->destroyQuery(q_addDirectoryLink);
	db->destroyQuery(q_removeDirectoryLink);
	db->destroyQuery(q_removeDirectoryLinkGlob);
	db->destroyQuery(q_getDirectoryRefcount);
	db->destroyQuery(q_addDirectoryLinkJournalEntry);
	db->destroyQuery(q_removeDirectoryLinkJournalEntry);
	db->destroyQuery(q_getDirectoryLinkJournalEntries);
	db->destroyQuery(q_removeDirectoryLinkJournalEntries);
	db->destroyQuery(q_getLinksInDirectory);
	db->destroyQuery(q_deleteLinkReferenceEntry);
	db->destroyQuery(q_updateLinkReferenceTarget);
	db->destroyQuery(q_addToOldBackupfolders);
	db->destroyQuery(q_getOldBackupfolders);
	db->destroyQuery(q_getDeletePendingClientNames);
	db->destroyQuery(q_createTemporaryLastFilesTable);
	db->destroyQuery(q_dropTemporaryLastFilesTable);
	db->destroyQuery(q_createTemporaryLastFilesTableIndex);
	db->destroyQuery(q_dropTemporaryLastFilesTableIndex);
	db->destroyQuery(q_copyToTemporaryLastFilesTable);
	db->destroyQuery(q_getFileEntryFromTemporaryTable);
	db->destroyQuery(q_getFileEntriesFromTemporaryTableGlob);
	db->destroyQuery(q_insertIntoOrigClientSettings);
	db->destroyQuery(q_getOrigClientSettings);
	db->destroyQuery(q_getLastIncrementalDurations);
	db->destroyQuery(q_getLastFullDurations);
	db->destroyQuery(q_setNextEntry);
	db->destroyQuery(q_setPrevEntry);
	db->destroyQuery(q_setPointedTo);
	db->destroyQuery(q_addFileEntry);
	db->destroyQuery(q_delFileEntry);
	db->destroyQuery(q_getFileEntry);
	db->destroyQuery(q_getStatFileEntry);
	db->destroyQuery(q_addIncomingFile);
	db->destroyQuery(q_getIncomingStats);
	db->destroyQuery(q_getIncomingStatsCount);
	db->destroyQuery(q_delIncomingStatEntry);
	db->destroyQuery(q_getMiscValue);
	db->destroyQuery(q_addMiscValue);
	db->destroyQuery(q_delMiscValue);
	db->destroyQuery(q_setClientUsedFilebackupSize);
}

void ServerBackupDao::commit()
{
	db->Write("PRAGMA wal_checkpoint");
}

int64 ServerBackupDao::getLastId()
{
	return db->getLastInsertID();
}

void ServerBackupDao::updateOrInsertSetting( int clientid, const std::wstring& key, const std::wstring& value )
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

void ServerBackupDao::beginTransaction()
{
	db->BeginTransaction();
}

void ServerBackupDao::endTransaction()
{
	db->EndTransaction();
}

int64 ServerBackupDao::addFileEntryExternal( int backupid, const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to )
{
	addFileEntry(backupid, fullpath, hashpath, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, pointed_to);

	int64 id = db->getLastInsertID();

	if(prev_entry!=0)
	{
		setNextEntry(id, prev_entry);
	}

	if(next_entry!=0)
	{
		setPrevEntry(id, next_entry);
	}

	return id;
}

void ServerBackupDao::detachDbs()
{
	db->DetachDBs();
}

void ServerBackupDao::attachDbs()
{
	db->AttachDBs();
}
