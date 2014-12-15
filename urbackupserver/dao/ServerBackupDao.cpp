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
* @func string ServerBackupDao::getClientSetting
* @return string value
* @sql
*      SELECT value FROM settings_db.settings WHERE key=:key(string) AND clientid=:clientid(int)
*/
ServerBackupDao::CondString ServerBackupDao::getClientSetting(const std::wstring& key, int clientid)
{
	if(q_getClientSetting==NULL)
	{
		q_getClientSetting=db->Prepare("SELECT value FROM settings_db.settings WHERE key=? AND clientid=?", false);
	}
	q_getClientSetting->Bind(key);
	q_getClientSetting->Bind(clientid);
	db_results res=q_getClientSetting->Read();
	q_getClientSetting->Reset();
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
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::getPointedTo
* @return int64 pointed_to
* @sql
*      SELECT pointed_to FROM files WHERE id=:id(int64)
*/
ServerBackupDao::CondInt64 ServerBackupDao::getPointedTo(int64 id)
{
	if(q_getPointedTo==NULL)
	{
		q_getPointedTo=db->Prepare("SELECT pointed_to FROM files WHERE id=?", false);
	}
	q_getPointedTo->Bind(id);
	db_results res=q_getPointedTo->Read();
	q_getPointedTo->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"pointed_to"]);
	}
	return ret;
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

/**
* @-SQLGenAccessNoCheck
* @func bool ServerBackupDao::createTemporaryPathLookupTable
* @sql
*      CREATE TEMPORARY TABLE files_cont_path_lookup ( fullpath TEXT, entryid INTEGER);
*/
bool ServerBackupDao::createTemporaryPathLookupTable(void)
{
	if(q_createTemporaryPathLookupTable==NULL)
	{
		q_createTemporaryPathLookupTable=db->Prepare("CREATE TEMPORARY TABLE files_cont_path_lookup ( fullpath TEXT, entryid INTEGER);", false);
	}
	bool ret = q_createTemporaryPathLookupTable->Write();
	return ret;
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerBackupDao::dropTemporaryPathLookupTable
* @sql
*      DROP TABLE files_cont_path_lookup
*/
void ServerBackupDao::dropTemporaryPathLookupTable(void)
{
	if(q_dropTemporaryPathLookupTable==NULL)
	{
		q_dropTemporaryPathLookupTable=db->Prepare("DROP TABLE files_cont_path_lookup", false);
	}
	q_dropTemporaryPathLookupTable->Write();
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerBackupDao::dropTemporaryPathLookupIndex
* @sql
*      DROP INDEX files_cont_path_lookup_idx
*/
void ServerBackupDao::dropTemporaryPathLookupIndex(void)
{
	if(q_dropTemporaryPathLookupIndex==NULL)
	{
		q_dropTemporaryPathLookupIndex=db->Prepare("DROP INDEX files_cont_path_lookup_idx", false);
	}
	q_dropTemporaryPathLookupIndex->Write();
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerBackupDao::populateTemporaryPathLookupTable
* @sql
*      INSERT INTO files_cont_path_lookup (fullpath, entryid)
*		 SELECT fullpath, id AS entryid FROM files WHERE backupid=:backupid(int)
*/
void ServerBackupDao::populateTemporaryPathLookupTable(int backupid)
{
	if(q_populateTemporaryPathLookupTable==NULL)
	{
		q_populateTemporaryPathLookupTable=db->Prepare("INSERT INTO files_cont_path_lookup (fullpath, entryid) SELECT fullpath, id AS entryid FROM files WHERE backupid=?", false);
	}
	q_populateTemporaryPathLookupTable->Bind(backupid);
	q_populateTemporaryPathLookupTable->Write();
	q_populateTemporaryPathLookupTable->Reset();
}

/**
* @-SQLGenAccessNoCheck
* @func bool ServerBackupDao::createTemporaryPathLookupIndex
* @sql
*      CREATE INDEX files_cont_path_lookup_idx ON files_cont_path_lookup ( fullpath );
*/
bool ServerBackupDao::createTemporaryPathLookupIndex(void)
{
	if(q_createTemporaryPathLookupIndex==NULL)
	{
		q_createTemporaryPathLookupIndex=db->Prepare("CREATE INDEX files_cont_path_lookup_idx ON files_cont_path_lookup ( fullpath );", false);
	}
	bool ret = q_createTemporaryPathLookupIndex->Write();
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerBackupDao::lookupEntryIdByPath
* @return int64 entryid
* @sql
*       SELECT entryid FROM files_cont_path_lookup WHERE fullpath=:fullpath(string)
*/
ServerBackupDao::CondInt64 ServerBackupDao::lookupEntryIdByPath(const std::wstring& fullpath)
{
	if(q_lookupEntryIdByPath==NULL)
	{
		q_lookupEntryIdByPath=db->Prepare("SELECT entryid FROM files_cont_path_lookup WHERE fullpath=?", false);
	}
	q_lookupEntryIdByPath->Bind(fullpath);
	db_results res=q_lookupEntryIdByPath->Read();
	q_lookupEntryIdByPath->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"entryid"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::newFileBackup
* @sql
*       INSERT INTO backups (incremental, clientid, path, complete, running, size_bytes, done, archived, size_calculated, resumed, indexing_time_ms, tgroup)
*		VALUES (:incremental(int), :clientid(int), :path(string), 0, CURRENT_TIMESTAMP, -1, 0, 0, 0, :resumed(int), :indexing_time_ms(int64), :tgroup(int) )
*/
void ServerBackupDao::newFileBackup(int incremental, int clientid, const std::wstring& path, int resumed, int64 indexing_time_ms, int tgroup)
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
	q_newFileBackup->Write();
	q_newFileBackup->Reset();
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
	SLastIncremental ret = { false, 0, L"", 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.incremental=watoi(res[0][L"incremental"]);
		ret.path=res[0][L"path"];
		ret.resumed=watoi(res[0][L"resumed"]);
		ret.complete=watoi(res[0][L"complete"]);
		ret.id=watoi(res[0][L"id"]);
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
	SLastIncremental ret = { false, 0, L"", 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.incremental=watoi(res[0][L"incremental"]);
		ret.path=res[0][L"path"];
		ret.resumed=watoi(res[0][L"resumed"]);
		ret.complete=watoi(res[0][L"complete"]);
		ret.id=watoi(res[0][L"id"]);
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
*       INSERT INTO logs (clientid, errors, warnings, infos, image, incremental, resumed)
*               VALUES (:clientid(int), :errors(int), :warnings(int), :infos(int), :image(int), :incremental(int), :resumed(int) )
*/
void ServerBackupDao::saveBackupLog(int clientid, int errors, int warnings, int infos, int image, int incremental, int resumed)
{
	if(q_saveBackupLog==NULL)
	{
		q_saveBackupLog=db->Prepare("INSERT INTO logs (clientid, errors, warnings, infos, image, incremental, resumed) VALUES (?, ?, ?, ?, ?, ?, ? )", false);
	}
	q_saveBackupLog->Bind(clientid);
	q_saveBackupLog->Bind(errors);
	q_saveBackupLog->Bind(warnings);
	q_saveBackupLog->Bind(infos);
	q_saveBackupLog->Bind(image);
	q_saveBackupLog->Bind(incremental);
	q_saveBackupLog->Bind(resumed);
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
void ServerBackupDao::saveBackupLogData(int64 logid, const std::wstring& data)
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
		ret[i]=watoi(res[i][L"id"]);
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
ServerBackupDao::CondString ServerBackupDao::getUserRight(int clientid, const std::wstring& t_domain)
{
	if(q_getUserRight==NULL)
	{
		q_getUserRight=db->Prepare("SELECT t_right FROM settings_db.si_permissions WHERE clientid=? AND t_domain=?", false);
	}
	q_getUserRight->Bind(clientid);
	q_getUserRight->Bind(t_domain);
	db_results res=q_getUserRight->Read();
	q_getUserRight->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"t_right"];
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
	SReportSettings ret = { false, L"", 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.report_mail=res[0][L"report_mail"];
		ret.report_loglevel=watoi(res[0][L"report_loglevel"]);
		ret.report_sendonly=watoi(res[0][L"report_sendonly"]);
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
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"time"];
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
ServerBackupDao::SImageBackup ServerBackupDao::getLastFullImage(int clientid, int image_version, const std::wstring& letter)
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
	SImageBackup ret = { false, 0, 0, L"", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0][L"id"]);
		ret.incremental=watoi(res[0][L"incremental"]);
		ret.path=res[0][L"path"];
		ret.duration=watoi64(res[0][L"duration"]);
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
ServerBackupDao::SImageBackup ServerBackupDao::getLastImage(int clientid, int image_version, const std::wstring& letter)
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
	SImageBackup ret = { false, 0, 0, L"", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0][L"id"]);
		ret.incremental=watoi(res[0][L"incremental"]);
		ret.path=res[0][L"path"];
		ret.duration=watoi64(res[0][L"duration"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::newImageBackup
* @sql
*       INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes, version, letter)
*			VALUES (:clientid(int), :path(string), :incremental(int), :incremental_ref(int), 0, CURRENT_TIMESTAMP, 0, :image_version(int), :letter(string) )
*/
void ServerBackupDao::newImageBackup(int clientid, const std::wstring& path, int incremental, int incremental_ref, int image_version, const std::wstring& letter)
{
	if(q_newImageBackup==NULL)
	{
		q_newImageBackup=db->Prepare("INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes, version, letter) VALUES (?, ?, ?, ?, 0, CURRENT_TIMESTAMP, 0, ?, ? )", false);
	}
	q_newImageBackup->Bind(clientid);
	q_newImageBackup->Bind(path);
	q_newImageBackup->Bind(incremental);
	q_newImageBackup->Bind(incremental_ref);
	q_newImageBackup->Bind(image_version);
	q_newImageBackup->Bind(letter);
	q_newImageBackup->Write();
	q_newImageBackup->Reset();
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
*       UPDATE clients SET lastbackup_image=(SELECT b.backuptime FROM backup_images b WHERE b.id=:backupid(int)) WHERE id=:clientid(int)
*/
void ServerBackupDao::updateClientLastImageBackup(int backupid, int clientid)
{
	if(q_updateClientLastImageBackup==NULL)
	{
		q_updateClientLastImageBackup=db->Prepare("UPDATE clients SET lastbackup_image=(SELECT b.backuptime FROM backup_images b WHERE b.id=?) WHERE id=?", false);
	}
	q_updateClientLastImageBackup->Bind(backupid);
	q_updateClientLastImageBackup->Bind(clientid);
	q_updateClientLastImageBackup->Write();
	q_updateClientLastImageBackup->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::updateClientLastFileBackup
* @sql
*       UPDATE clients SET lastbackup=(SELECT b.backuptime FROM backups b WHERE b.id=:backupid(int)) WHERE id=:clientid(int)
*/
void ServerBackupDao::updateClientLastFileBackup(int backupid, int clientid)
{
	if(q_updateClientLastFileBackup==NULL)
	{
		q_updateClientLastFileBackup=db->Prepare("UPDATE clients SET lastbackup=(SELECT b.backuptime FROM backups b WHERE b.id=?) WHERE id=?", false);
	}
	q_updateClientLastFileBackup->Bind(backupid);
	q_updateClientLastFileBackup->Bind(clientid);
	q_updateClientLastFileBackup->Write();
	q_updateClientLastFileBackup->Reset();
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
*       INSERT INTO users_on_client (clientid, username) VALUES (:clientid(int), :username(string))
*/
void ServerBackupDao::addUserOnClient(int clientid, const std::wstring& username)
{
	if(q_addUserOnClient==NULL)
	{
		q_addUserOnClient=db->Prepare("INSERT INTO users_on_client (clientid, username) VALUES (?, ?)", false);
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
*       INSERT OR IGNORE INTO tokens_on_client (clientid, token) VALUES (:clientid(int), :token(string))
*/
void ServerBackupDao::addClientToken(int clientid, const std::wstring& token)
{
	if(q_addClientToken==NULL)
	{
		q_addClientToken=db->Prepare("INSERT OR IGNORE INTO tokens_on_client (clientid, token) VALUES (?, ?)", false);
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
*       INSERT OR IGNORE INTO user_tokens (username, token) VALUES (:username(string), :token(string))
*/
void ServerBackupDao::addUserToken(const std::wstring& username, const std::wstring& token)
{
	if(q_addUserToken==NULL)
	{
		q_addUserToken=db->Prepare("INSERT OR IGNORE INTO user_tokens (username, token) VALUES (?, ?)", false);
	}
	q_addUserToken->Bind(username);
	q_addUserToken->Bind(token);
	q_addUserToken->Write();
	q_addUserToken->Reset();
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
	q_getClientSetting=NULL;
	q_getClientIds=NULL;
	q_getPointedTo=NULL;
	q_addFileEntry=NULL;
	q_getSetting=NULL;
	q_insertSetting=NULL;
	q_updateSetting=NULL;
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
	q_createTemporaryPathLookupTable=NULL;
	q_dropTemporaryPathLookupTable=NULL;
	q_dropTemporaryPathLookupIndex=NULL;
	q_populateTemporaryPathLookupTable=NULL;
	q_createTemporaryPathLookupIndex=NULL;
	q_lookupEntryIdByPath=NULL;
	q_newFileBackup=NULL;
	q_updateFileBackupRunning=NULL;
	q_setFileBackupDone=NULL;
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
	q_setImageBackupComplete=NULL;
	q_updateImageBackupRunning=NULL;
	q_saveImageAssociation=NULL;
	q_updateClientLastImageBackup=NULL;
	q_updateClientLastFileBackup=NULL;
	q_deleteAllUsersOnClient=NULL;
	q_addUserOnClient=NULL;
	q_addClientToken=NULL;
	q_addUserToken=NULL;
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
	db->destroyQuery(q_getClientSetting);
	db->destroyQuery(q_getClientIds);
	db->destroyQuery(q_getPointedTo);
	db->destroyQuery(q_addFileEntry);
	db->destroyQuery(q_getSetting);
	db->destroyQuery(q_insertSetting);
	db->destroyQuery(q_updateSetting);
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
	db->destroyQuery(q_createTemporaryPathLookupTable);
	db->destroyQuery(q_dropTemporaryPathLookupTable);
	db->destroyQuery(q_dropTemporaryPathLookupIndex);
	db->destroyQuery(q_populateTemporaryPathLookupTable);
	db->destroyQuery(q_createTemporaryPathLookupIndex);
	db->destroyQuery(q_lookupEntryIdByPath);
	db->destroyQuery(q_newFileBackup);
	db->destroyQuery(q_updateFileBackupRunning);
	db->destroyQuery(q_setFileBackupDone);
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
	db->destroyQuery(q_setImageBackupComplete);
	db->destroyQuery(q_updateImageBackupRunning);
	db->destroyQuery(q_saveImageAssociation);
	db->destroyQuery(q_updateClientLastImageBackup);
	db->destroyQuery(q_updateClientLastFileBackup);
	db->destroyQuery(q_deleteAllUsersOnClient);
	db->destroyQuery(q_addUserOnClient);
	db->destroyQuery(q_addClientToken);
	db->destroyQuery(q_addUserToken);
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

int ServerBackupDao::getLastChanges()
{
	return db->getLastChanges();
}
