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
}

void ServerBackupDao::commit()
{
	db->Write("PRAGMA wal_checkpoint");
}

int64 ServerBackupDao::getLastId()
{
	return db->getLastInsertID();
}
