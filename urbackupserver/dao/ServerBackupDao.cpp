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

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TEMPORARY TABLE files_last ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER);
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TEMPORARY TABLE files_new_tmp ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE DEFAULT CURRENT_TIMESTAMP );
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
*      INSERT INTO files_last (fullpath, hashpath, shahash, filesize)
*			SELECT fullpath, hashpath, shahash, filesize FROM files
*				WHERE backupid = :backupid(int)
*/
bool ServerBackupDao::copyToTemporaryLastFilesTable(int backupid)
{
	if(q_copyToTemporaryLastFilesTable==NULL)
	{
		q_copyToTemporaryLastFilesTable=db->Prepare("INSERT INTO files_last (fullpath, hashpath, shahash, filesize) SELECT fullpath, hashpath, shahash, filesize FROM files WHERE backupid = ?", false);
	}
	q_copyToTemporaryLastFilesTable->Bind(backupid);
	bool ret = q_copyToTemporaryLastFilesTable->Write();
	q_copyToTemporaryLastFilesTable->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func SFileEntry ServerBackupDao::getFileEntryFromTemporaryTable
* @return string fullpath, string hashpath, blob shahash, int64 filesize
* @sql
*      SELECT fullpath, hashpath, shahash, filesize
*       FROM files_last WHERE fullpath = :fullpath(string)
*/
ServerBackupDao::SFileEntry ServerBackupDao::getFileEntryFromTemporaryTable(const std::wstring& fullpath)
{
	if(q_getFileEntryFromTemporaryTable==NULL)
	{
		q_getFileEntryFromTemporaryTable=db->Prepare("SELECT fullpath, hashpath, shahash, filesize FROM files_last WHERE fullpath = ?", false);
	}
	q_getFileEntryFromTemporaryTable->Bind(fullpath);
	db_results res=q_getFileEntryFromTemporaryTable->Read();
	q_getFileEntryFromTemporaryTable->Reset();
	SFileEntry ret = { false, L"", L"", "", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.fullpath=res[0][L"fullpath"];
		ret.hashpath=res[0][L"hashpath"];
		std::wstring& val1 = res[0][L"shahash"];
		ret.shahash.resize(val1.size()*sizeof(wchar_t));
		memcpy(&ret.shahash[0], val1.data(), val1.size()*sizeof(wchar_t));
		ret.filesize=watoi64(res[0][L"filesize"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SFileEntry> ServerBackupDao::getFileEntriesFromTemporaryTableGlob
* @return string fullpath, string hashpath, blob shahash, int64 filesize
* @sql
*      SELECT fullpath, hashpath, shahash, filesize
*       FROM files_last WHERE fullpath GLOB :fullpath_glob(string)
*/
std::vector<ServerBackupDao::SFileEntry> ServerBackupDao::getFileEntriesFromTemporaryTableGlob(const std::wstring& fullpath_glob)
{
	if(q_getFileEntriesFromTemporaryTableGlob==NULL)
	{
		q_getFileEntriesFromTemporaryTableGlob=db->Prepare("SELECT fullpath, hashpath, shahash, filesize FROM files_last WHERE fullpath GLOB ?", false);
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
* @-SQLGenAccessNoCheck
* @func bool ServerBackupDao::createTemporaryNewFilesTable
* @sql
*      CREATE TEMPORARY TABLE files_new_tmp ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE DEFAULT CURRENT_TIMESTAMP );
*/
bool ServerBackupDao::createTemporaryNewFilesTable(void)
{
	if(q_createTemporaryNewFilesTable==NULL)
	{
		q_createTemporaryNewFilesTable=db->Prepare("CREATE TEMPORARY TABLE files_new_tmp ( fullpath TEXT, hashpath TEXT, shahash BLOB, filesize INTEGER, created DATE DEFAULT CURRENT_TIMESTAMP );", false);
	}
	bool ret = q_createTemporaryNewFilesTable->Write();
	return ret;
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerBackupDao::dropTemporaryNewFilesTable
* @sql
*      DROP TABLE files_new_tmp
*/
void ServerBackupDao::dropTemporaryNewFilesTable(void)
{
	if(q_dropTemporaryNewFilesTable==NULL)
	{
		q_dropTemporaryNewFilesTable=db->Prepare("DROP TABLE files_new_tmp", false);
	}
	q_dropTemporaryNewFilesTable->Write();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::insertIntoTemporaryNewFilesTable
* @sql
*      INSERT INTO files_new_tmp ( fullpath, hashpath, shahash, filesize)
*         VALUES ( :fullpath(string), :hashpath(string), :shahash(blob), :filesize(int64) )
*/
void ServerBackupDao::insertIntoTemporaryNewFilesTable(const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize)
{
	if(q_insertIntoTemporaryNewFilesTable==NULL)
	{
		q_insertIntoTemporaryNewFilesTable=db->Prepare("INSERT INTO files_new_tmp ( fullpath, hashpath, shahash, filesize) VALUES ( ?, ?, ?, ? )", false);
	}
	q_insertIntoTemporaryNewFilesTable->Bind(fullpath);
	q_insertIntoTemporaryNewFilesTable->Bind(hashpath);
	q_insertIntoTemporaryNewFilesTable->Bind(shahash.c_str(), (_u32)shahash.size());
	q_insertIntoTemporaryNewFilesTable->Bind(filesize);
	q_insertIntoTemporaryNewFilesTable->Write();
	q_insertIntoTemporaryNewFilesTable->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::copyFromTemporaryNewFilesTableToFilesTable
* @sql
*      INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, created, rsize, did_count, clientid, incremental)
*          SELECT :backupid(int) AS backupid, fullpath, hashpath,
*                 shahash, filesize, created, 0 AS rsize, 0 AS did_count, :clientid(int) AS clientid,
*                 :incremental(int) AS incremental FROM files_new_tmp
*/
void ServerBackupDao::copyFromTemporaryNewFilesTableToFilesTable(int backupid, int clientid, int incremental)
{
	if(q_copyFromTemporaryNewFilesTableToFilesTable==NULL)
	{
		q_copyFromTemporaryNewFilesTableToFilesTable=db->Prepare("INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, created, rsize, did_count, clientid, incremental) SELECT ? AS backupid, fullpath, hashpath, shahash, filesize, created, 0 AS rsize, 0 AS did_count, ? AS clientid, ? AS incremental FROM files_new_tmp", false);
	}
	q_copyFromTemporaryNewFilesTableToFilesTable->Bind(backupid);
	q_copyFromTemporaryNewFilesTableToFilesTable->Bind(clientid);
	q_copyFromTemporaryNewFilesTableToFilesTable->Bind(incremental);
	q_copyFromTemporaryNewFilesTableToFilesTable->Write();
	q_copyFromTemporaryNewFilesTableToFilesTable->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerBackupDao::copyFromTemporaryNewFilesTableToFilesNewTable
* @sql
*      INSERT INTO files_new (backupid, fullpath, hashpath, shahash, filesize, created, rsize, clientid, incremental)
*          SELECT :backupid(int) AS backupid, fullpath, hashpath,
*                 shahash, filesize, created, 0 AS rsize, :clientid(int) AS clientid,
*                 :incremental(int) AS incremental FROM files_new_tmp
*/
void ServerBackupDao::copyFromTemporaryNewFilesTableToFilesNewTable(int backupid, int clientid, int incremental)
{
	if(q_copyFromTemporaryNewFilesTableToFilesNewTable==NULL)
	{
		q_copyFromTemporaryNewFilesTableToFilesNewTable=db->Prepare("INSERT INTO files_new (backupid, fullpath, hashpath, shahash, filesize, created, rsize, clientid, incremental) SELECT ? AS backupid, fullpath, hashpath, shahash, filesize, created, 0 AS rsize, ? AS clientid, ? AS incremental FROM files_new_tmp", false);
	}
	q_copyFromTemporaryNewFilesTableToFilesNewTable->Bind(backupid);
	q_copyFromTemporaryNewFilesTableToFilesNewTable->Bind(clientid);
	q_copyFromTemporaryNewFilesTableToFilesNewTable->Bind(incremental);
	q_copyFromTemporaryNewFilesTableToFilesNewTable->Write();
	q_copyFromTemporaryNewFilesTableToFilesNewTable->Reset();
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
	q_createTemporaryNewFilesTable=NULL;
	q_dropTemporaryNewFilesTable=NULL;
	q_insertIntoTemporaryNewFilesTable=NULL;
	q_copyFromTemporaryNewFilesTableToFilesTable=NULL;
	q_copyFromTemporaryNewFilesTableToFilesNewTable=NULL;
	q_insertIntoOrigClientSettings=NULL;
	q_getOrigClientSettings=NULL;
	q_getLastIncrementalDurations=NULL;
	q_getLastFullDurations=NULL;
	q_getClientSetting=NULL;
	q_getClientIds=NULL;
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
	db->destroyQuery(q_createTemporaryNewFilesTable);
	db->destroyQuery(q_dropTemporaryNewFilesTable);
	db->destroyQuery(q_insertIntoTemporaryNewFilesTable);
	db->destroyQuery(q_copyFromTemporaryNewFilesTableToFilesTable);
	db->destroyQuery(q_copyFromTemporaryNewFilesTableToFilesNewTable);
	db->destroyQuery(q_insertIntoOrigClientSettings);
	db->destroyQuery(q_getOrigClientSettings);
	db->destroyQuery(q_getLastIncrementalDurations);
	db->destroyQuery(q_getLastFullDurations);
	db->destroyQuery(q_getClientSetting);
	db->destroyQuery(q_getClientIds);
}

void ServerBackupDao::commit()
{
	db->Write("PRAGMA wal_checkpoint");
}

int64 ServerBackupDao::getLastId()
{
	return db->getLastInsertID();
}

void ServerBackupDao::beginTransaction()
{
	db->BeginTransaction();
}

void ServerBackupDao::endTransaction()
{
	db->EndTransaction();
}
