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
*              AND name=:name(string)
*/
void ServerBackupDao::removeDirectoryLink(int clientid, const std::wstring& name)
{
	q_removeDirectoryLink->Bind(clientid);
	q_removeDirectoryLink->Bind(name);
	q_removeDirectoryLink->Write();
	q_removeDirectoryLink->Reset();
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

//@-SQLGenSetup
void ServerBackupDao::prepareQueries( void )
{
	q_addDirectoryLink=db->Prepare("INSERT INTO directory_links  (clientid, name, target) VALUES (?, ?, ?)", false);
	q_removeDirectoryLink=db->Prepare("DELETE FROM directory_links WHERE clientid=? AND name=?", false);
	q_getDirectoryRefcount=db->Prepare("SELECT COUNT(*) AS c FROM directory_links WHERE clientid=? AND name=? LIMIT 1", false);
	q_addDirectoryLinkJournalEntry=db->Prepare("INSERT INTO directory_link_journal (linkname, linktarget) VALUES (?, ?)", false);
	q_removeDirectoryLinkJournalEntry=db->Prepare("DELETE FROM directory_link_journal WHERE id = ?", false);
	q_getDirectoryLinkJournalEntries=db->Prepare("SELECT linkname, linktarget FROM directory_link_journal", false);
	q_removeDirectoryLinkJournalEntries=db->Prepare("DELETE FROM directory_link_journal", false);
	q_getLinksInDirectory=db->Prepare("SELECT name, target FROM directory_links WHERE clientid=? AND target GLOB ?", false);
}

//@-SQLGenDestruction
void ServerBackupDao::destroyQueries( void )
{
	db->destroyQuery(q_addDirectoryLink);
	db->destroyQuery(q_removeDirectoryLink);
	db->destroyQuery(q_getDirectoryRefcount);
	db->destroyQuery(q_addDirectoryLinkJournalEntry);
	db->destroyQuery(q_removeDirectoryLinkJournalEntry);
	db->destroyQuery(q_getDirectoryLinkJournalEntries);
	db->destroyQuery(q_removeDirectoryLinkJournalEntries);
	db->destroyQuery(q_getLinksInDirectory);
}

void ServerBackupDao::commit()
{
	db->Write("PRAGMA wal_checkpoint");
}

int64 ServerBackupDao::getLastId()
{
	return db->getLastInsertID();
}
