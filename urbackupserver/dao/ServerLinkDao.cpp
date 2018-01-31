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

#include "ServerLinkDao.h"
#include "../../stringtools.h"
#include <assert.h>
#include <string.h>

ServerLinkDao::ServerLinkDao(IDatabase * db)
	:db(db)
{
	prepareQueries();
}

ServerLinkDao::~ServerLinkDao()
{
	destroyQueries();
}

IDatabase * ServerLinkDao::getDatabase()
{
	return db;
}

int64 ServerLinkDao::getLastId()
{
	return db->getLastInsertID();
}

int ServerLinkDao::getLastChanges()
{
	return db->getLastChanges();
}

/**
* @-SQLGenAccess
* @func void ServerLinkDao::addDirectoryLink
* @sql
*	INSERT INTO directory_links
*		(clientid, name, target)
*	VALUES
*		(:clientid(int),
*		 :name(string),
*		 :target(string))
*/
void ServerLinkDao::addDirectoryLink(int clientid, const std::string& name, const std::string& target)
{
	if(q_addDirectoryLink==NULL)
	{
		q_addDirectoryLink=db->Prepare("INSERT INTO directory_links (clientid, name, target) VALUES (?, ?, ?)", false);
	}
	q_addDirectoryLink->Bind(clientid);
	q_addDirectoryLink->Bind(name);
	q_addDirectoryLink->Bind(target);
	q_addDirectoryLink->Write();
	q_addDirectoryLink->Reset();
}


/**
* @-SQLGenAccess
* @func void ServerLinkDao::removeDirectoryLink
* @sql
*     DELETE FROM directory_links
*          WHERE clientid=:clientid(int)
*			   AND target=:target(string)
*/
void ServerLinkDao::removeDirectoryLink(int clientid, const std::string& target)
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
* @func void ServerLinkDao::removeDirectoryLinkWithName
* @sql
*     DELETE FROM directory_links
*          WHERE clientid=:clientid(int)
*			   AND target=:target(string)
*			   AND name=:name(string)
*/
void ServerLinkDao::removeDirectoryLinkWithName(int clientid, const std::string& target, const std::string& name)
{
	if(q_removeDirectoryLinkWithName==NULL)
	{
		q_removeDirectoryLinkWithName=db->Prepare("DELETE FROM directory_links WHERE clientid=? AND target=? AND name=?", false);
	}
	q_removeDirectoryLinkWithName->Bind(clientid);
	q_removeDirectoryLinkWithName->Bind(target);
	q_removeDirectoryLinkWithName->Bind(name);
	q_removeDirectoryLinkWithName->Write();
	q_removeDirectoryLinkWithName->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerLinkDao::removeDirectoryLinkGlob
* @sql
*     DELETE FROM directory_links
*          WHERE clientid=:clientid(int)
*			   AND target GLOB :target(string)
*/
void ServerLinkDao::removeDirectoryLinkGlob(int clientid, const std::string& target)
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
* @func int ServerLinkDao::getDirectoryRefcount
* @return int_raw c
* @sql
*    SELECT COUNT(*) AS c FROM directory_links
*        WHERE clientid=:clientid(int)
*              AND name=:name(string)
*        LIMIT 1
*/
int ServerLinkDao::getDirectoryRefcount(int clientid, const std::string& name)
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
	return watoi(res[0]["c"]);
}

/**
* @-SQLGenAccess
* @func int ServerLinkDao::getDirectoryRefcountWithTarget
* @return int_raw c
* @sql
*    SELECT COUNT(*) AS c FROM directory_links
*        WHERE clientid=:clientid(int)
*              AND name=:name(string)
*			   AND target=:target(string)
*        LIMIT 1
*/
int ServerLinkDao::getDirectoryRefcountWithTarget(int clientid, const std::string& name, const std::string& target)
{
	if(q_getDirectoryRefcountWithTarget==NULL)
	{
		q_getDirectoryRefcountWithTarget=db->Prepare("SELECT COUNT(*) AS c FROM directory_links WHERE clientid=? AND name=? AND target=? LIMIT 1", false);
	}
	q_getDirectoryRefcountWithTarget->Bind(clientid);
	q_getDirectoryRefcountWithTarget->Bind(name);
	q_getDirectoryRefcountWithTarget->Bind(target);
	db_results res=q_getDirectoryRefcountWithTarget->Read();
	q_getDirectoryRefcountWithTarget->Reset();
	assert(!res.empty());
	return watoi(res[0]["c"]);
}

/**
* @-SQLGenAccess
* @func vector<DirectoryLinkEntry> ServerLinkDao::getLinksInDirectory
* @return string name, string target
* @sql
*     SELECT name, target FROM directory_links
*            WHERE clientid=:clientid(int) AND
*                  target GLOB :dir(string)
*/
std::vector<ServerLinkDao::DirectoryLinkEntry> ServerLinkDao::getLinksInDirectory(int clientid, const std::string& dir)
{
	if(q_getLinksInDirectory==NULL)
	{
		q_getLinksInDirectory=db->Prepare("SELECT name, target FROM directory_links WHERE clientid=? AND target GLOB ?", false);
	}
	q_getLinksInDirectory->Bind(clientid);
	q_getLinksInDirectory->Bind(dir);
	db_results res=q_getLinksInDirectory->Read();
	q_getLinksInDirectory->Reset();
	std::vector<ServerLinkDao::DirectoryLinkEntry> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].name=res[i]["name"];
		ret[i].target=res[i]["target"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<DirectoryLinkEntry> ServerLinkDao::getLinksByPoolName
* @return string name, string target
* @sql
*     SELECT name, target FROM directory_links
*            WHERE clientid=:clientid(int) AND
*                  name=:name(string)
*/
std::vector<ServerLinkDao::DirectoryLinkEntry> ServerLinkDao::getLinksByPoolName(int clientid, const std::string& name)
{
	if(q_getLinksByPoolName==NULL)
	{
		q_getLinksByPoolName=db->Prepare("SELECT name, target FROM directory_links WHERE clientid=? AND name=?", false);
	}
	q_getLinksByPoolName->Bind(clientid);
	q_getLinksByPoolName->Bind(name);
	db_results res=q_getLinksByPoolName->Read();
	q_getLinksByPoolName->Reset();
	std::vector<ServerLinkDao::DirectoryLinkEntry> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].name=res[i]["name"];
		ret[i].target=res[i]["target"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerLinkDao::deleteLinkReferenceEntry
* @sql
*     DELETE FROM directory_links
*            WHERE id=:id(int64)
*/
void ServerLinkDao::deleteLinkReferenceEntry(int64 id)
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
* @func void ServerLinkDao::updateLinkReferenceTarget
* @sql
*     UPDATE directory_links SET target=:new_target(string)
*            WHERE id=:id(int64)
*/
void ServerLinkDao::updateLinkReferenceTarget(const std::string& new_target, int64 id)
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

//@-SQLGenSetup
void ServerLinkDao::prepareQueries()
{
	q_addDirectoryLink=NULL;
	q_removeDirectoryLink=NULL;
	q_removeDirectoryLinkWithName=NULL;
	q_removeDirectoryLinkGlob=NULL;
	q_getDirectoryRefcount=NULL;
	q_getDirectoryRefcountWithTarget=NULL;
	q_getLinksInDirectory=NULL;
	q_getLinksByPoolName=NULL;
	q_deleteLinkReferenceEntry=NULL;
	q_updateLinkReferenceTarget=NULL;
}

//@-SQLGenDestruction
void ServerLinkDao::destroyQueries()
{
	db->destroyQuery(q_addDirectoryLink);
	db->destroyQuery(q_removeDirectoryLink);
	db->destroyQuery(q_removeDirectoryLinkWithName);
	db->destroyQuery(q_removeDirectoryLinkGlob);
	db->destroyQuery(q_getDirectoryRefcount);
	db->destroyQuery(q_getDirectoryRefcountWithTarget);
	db->destroyQuery(q_getLinksInDirectory);
	db->destroyQuery(q_getLinksByPoolName);
	db->destroyQuery(q_deleteLinkReferenceEntry);
	db->destroyQuery(q_updateLinkReferenceTarget);
}