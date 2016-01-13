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


#include "ServerLinkJournalDao.h"
#include "../../stringtools.h"
#include <assert.h>
#include <string.h>

ServerLinkJournalDao::ServerLinkJournalDao(IDatabase * db)
	:db(db)
{
	prepareQueries();
}

ServerLinkJournalDao::~ServerLinkJournalDao()
{
	destroyQueries();
}

/**
* @-SQLGenAccess
* @func void ServerLinkJournalDao::addDirectoryLinkJournalEntry
* @sql
*    INSERT INTO directory_link_journal (linkname, linktarget)
*     VALUES (:linkname(string), :linktarget(string))
*/
void ServerLinkJournalDao::addDirectoryLinkJournalEntry(const std::string& linkname, const std::string& linktarget)
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
* @func void ServerLinkJournalDao::removeDirectoryLinkJournalEntry
* @sql
*     DELETE FROM directory_link_journal WHERE
*             id = :entry_id(int64)
*/
void ServerLinkJournalDao::removeDirectoryLinkJournalEntry(int64 entry_id)
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
* @func vector<JournalEntry> ServerLinkJournalDao::getDirectoryLinkJournalEntries
* @return string linkname, string linktarget
* @sql
*     SELECT linkname, linktarget FROM directory_link_journal
*/
std::vector<ServerLinkJournalDao::JournalEntry> ServerLinkJournalDao::getDirectoryLinkJournalEntries(void)
{
	if(q_getDirectoryLinkJournalEntries==NULL)
	{
		q_getDirectoryLinkJournalEntries=db->Prepare("SELECT linkname, linktarget FROM directory_link_journal", false);
	}
	db_results res=q_getDirectoryLinkJournalEntries->Read();
	std::vector<ServerLinkJournalDao::JournalEntry> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].linkname=res[i]["linkname"];
		ret[i].linktarget=res[i]["linktarget"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerLinkJournalDao::removeDirectoryLinkJournalEntries
* @sql
*     DELETE FROM directory_link_journal
*/
void ServerLinkJournalDao::removeDirectoryLinkJournalEntries(void)
{
	if(q_removeDirectoryLinkJournalEntries==NULL)
	{
		q_removeDirectoryLinkJournalEntries=db->Prepare("DELETE FROM directory_link_journal", false);
	}
	q_removeDirectoryLinkJournalEntries->Write();
}

//@-SQLGenSetup
void ServerLinkJournalDao::prepareQueries()
{
	q_addDirectoryLinkJournalEntry=NULL;
	q_removeDirectoryLinkJournalEntry=NULL;
	q_getDirectoryLinkJournalEntries=NULL;
	q_removeDirectoryLinkJournalEntries=NULL;
}

//@-SQLGenDestruction
void ServerLinkJournalDao::destroyQueries()
{
	db->destroyQuery(q_addDirectoryLinkJournalEntry);
	db->destroyQuery(q_removeDirectoryLinkJournalEntry);
	db->destroyQuery(q_getDirectoryLinkJournalEntries);
	db->destroyQuery(q_removeDirectoryLinkJournalEntries);
}