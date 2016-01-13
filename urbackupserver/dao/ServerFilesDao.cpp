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

#include "ServerFilesDao.h"
#include "../../stringtools.h"
#include <assert.h>
#include <string.h>

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TEMPORARY TABLE files_cont_path_lookup ( fullpath TEXT, entryid INTEGER);
*/

const int ServerFilesDao::c_direction_incoming = 0;
const int ServerFilesDao::c_direction_outgoing = 1;
const int ServerFilesDao::c_direction_outgoing_nobackupstat = 2;

ServerFilesDao::ServerFilesDao(IDatabase * db)
	: db(db)
{
	prepareQueries();
}

ServerFilesDao::~ServerFilesDao()
{
	destroyQueries();
}

int64 ServerFilesDao::getLastId()
{
	return db->getLastInsertID();
}

int ServerFilesDao::getLastChanges()
{
	return db->getLastChanges();
}

void ServerFilesDao::BeginWriteTransaction()
{
	db->BeginWriteTransaction();
}

void ServerFilesDao::endTransaction()
{
	db->EndTransaction();
}

IDatabase * ServerFilesDao::getDatabase()
{
	return db;
}

/**
* @-SQLGenAccess
* @func void ServerFilesDao::setNextEntry
* @sql
*      UPDATE files SET next_entry=:next_entry(int64) WHERE id=:id(int64)
*/
void ServerFilesDao::setNextEntry(int64 next_entry, int64 id)
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
* @func void ServerFilesDao::setPrevEntry
* @sql
*      UPDATE files SET prev_entry=:prev_entry(int64) WHERE id=:id(int64)
*/
void ServerFilesDao::setPrevEntry(int64 prev_entry, int64 id)
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
* @func void ServerFilesDao::setPointedTo
* @sql
*      UPDATE files SET pointed_to=:pointed_to(int64) WHERE id=:id(int64)
*/
void ServerFilesDao::setPointedTo(int64 pointed_to, int64 id)
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
* @func int64 ServerFilesDao::getPointedTo
* @return int64 pointed_to
* @sql
*      SELECT pointed_to FROM files WHERE id=:id(int64)
*/
ServerFilesDao::CondInt64 ServerFilesDao::getPointedTo(int64 id)
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
		ret.value=watoi64(res[0]["pointed_to"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerFilesDao::delFileEntry
* @sql
*	   DELETE FROM files WHERE id=:id(int64)
*/
void ServerFilesDao::delFileEntry(int64 id)
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
* @func SFindFileEntry ServerFilesDao::getFileEntry
* @return int64 id, string shahash, int backupid, int clientid, string fullpath, string hashpath, int64 filesize, int64 next_entry, int64 prev_entry, int64 rsize, int incremental, int pointed_to
* @sql
*	   SELECT id, shahash, backupid, clientid, fullpath, hashpath, filesize, next_entry, prev_entry, rsize, incremental, pointed_to
*      FROM files WHERE id=:id(int64)
*/
ServerFilesDao::SFindFileEntry ServerFilesDao::getFileEntry(int64 id)
{
	if(q_getFileEntry==NULL)
	{
		q_getFileEntry=db->Prepare("SELECT id, shahash, backupid, clientid, fullpath, hashpath, filesize, next_entry, prev_entry, rsize, incremental, pointed_to FROM files WHERE id=?", false);
	}
	q_getFileEntry->Bind(id);
	db_results res=q_getFileEntry->Read();
	q_getFileEntry->Reset();
	SFindFileEntry ret = { false, 0, "", 0, 0, "", "", 0, 0, 0, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.shahash=res[0]["shahash"];
		ret.backupid=watoi(res[0]["backupid"]);
		ret.clientid=watoi(res[0]["clientid"]);
		ret.fullpath=res[0]["fullpath"];
		ret.hashpath=res[0]["hashpath"];
		ret.filesize=watoi64(res[0]["filesize"]);
		ret.next_entry=watoi64(res[0]["next_entry"]);
		ret.prev_entry=watoi64(res[0]["prev_entry"]);
		ret.rsize=watoi64(res[0]["rsize"]);
		ret.incremental=watoi(res[0]["incremental"]);
		ret.pointed_to=watoi(res[0]["pointed_to"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SStatFileEntry ServerFilesDao::getStatFileEntry
* @return int64 id, int backupid, int clientid, int64 filesize, int64 rsize, string shahash, int64 next_entry, int64 prev_entry
* @sql
*	   SELECT id, backupid, clientid, filesize, rsize, shahash, next_entry, prev_entry
*      FROM files WHERE id=:id(int64)
*/
ServerFilesDao::SStatFileEntry ServerFilesDao::getStatFileEntry(int64 id)
{
	if(q_getStatFileEntry==NULL)
	{
		q_getStatFileEntry=db->Prepare("SELECT id, backupid, clientid, filesize, rsize, shahash, next_entry, prev_entry FROM files WHERE id=?", false);
	}
	q_getStatFileEntry->Bind(id);
	db_results res=q_getStatFileEntry->Read();
	q_getStatFileEntry->Reset();
	SStatFileEntry ret = { false, 0, 0, 0, 0, 0, "", 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.backupid=watoi(res[0]["backupid"]);
		ret.clientid=watoi(res[0]["clientid"]);
		ret.filesize=watoi64(res[0]["filesize"]);
		ret.rsize=watoi64(res[0]["rsize"]);
		ret.shahash=res[0]["shahash"];
		ret.next_entry=watoi64(res[0]["next_entry"]);
		ret.prev_entry=watoi64(res[0]["prev_entry"]);
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func void ServerFilesDao::addFileEntry
* @sql
*	   INSERT INTO files (backupid, fullpath, hashpath, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, pointed_to)
*      VALUES (:backupid(int), :fullpath(string), :hashpath(string), :shahash(blob),
*				:filesize(int64), :rsize(int64), :clientid(int), :incremental(int), :next_entry(int64), :prev_entry(int64), :pointed_to(int))
*/
void ServerFilesDao::addFileEntry(int backupid, const std::string& fullpath, const std::string& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to)
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
* @-SQLGenAccessNoCheck
* @func bool ServerFilesDao::createTemporaryPathLookupTable
* @sql
*      CREATE TEMPORARY TABLE files_cont_path_lookup ( fullpath TEXT, entryid INTEGER);
*/
bool ServerFilesDao::createTemporaryPathLookupTable(void)
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
* @func void ServerFilesDao::dropTemporaryPathLookupTable
* @sql
*      DROP TABLE files_cont_path_lookup
*/
void ServerFilesDao::dropTemporaryPathLookupTable(void)
{
	if(q_dropTemporaryPathLookupTable==NULL)
	{
		q_dropTemporaryPathLookupTable=db->Prepare("DROP TABLE files_cont_path_lookup", false);
	}
	q_dropTemporaryPathLookupTable->Write();
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerFilesDao::dropTemporaryPathLookupIndex
* @sql
*      DROP INDEX files_cont_path_lookup_idx
*/
void ServerFilesDao::dropTemporaryPathLookupIndex(void)
{
	if(q_dropTemporaryPathLookupIndex==NULL)
	{
		q_dropTemporaryPathLookupIndex=db->Prepare("DROP INDEX files_cont_path_lookup_idx", false);
	}
	q_dropTemporaryPathLookupIndex->Write();
}

/**
* @-SQLGenAccessNoCheck
* @func void ServerFilesDao::populateTemporaryPathLookupTable
* @sql
*      INSERT INTO files_cont_path_lookup (fullpath, entryid)
*		 SELECT fullpath, id AS entryid FROM files WHERE backupid=:backupid(int)
*/
void ServerFilesDao::populateTemporaryPathLookupTable(int backupid)
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
* @func bool ServerFilesDao::createTemporaryPathLookupIndex
* @sql
*      CREATE INDEX files_cont_path_lookup_idx ON files_cont_path_lookup ( fullpath );
*/
bool ServerFilesDao::createTemporaryPathLookupIndex(void)
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
* @func int64 ServerFilesDao::lookupEntryIdByPath
* @return int64 entryid
* @sql
*       SELECT entryid FROM files_cont_path_lookup WHERE fullpath=:fullpath(string)
*/
ServerFilesDao::CondInt64 ServerFilesDao::lookupEntryIdByPath(const std::string& fullpath)
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
		ret.value=watoi64(res[0]["entryid"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerFilesDao::addIncomingFile
* @sql
*       INSERT INTO files_incoming_stat (filesize, clientid, backupid, existing_clients, direction, incremental)
*       VALUES (:filesize(int64), :clientid(int), :backupid(int), :existing_clients(string), :direction(int), :incremental(int))
*/
void ServerFilesDao::addIncomingFile(int64 filesize, int clientid, int backupid, const std::string& existing_clients, int direction, int incremental)
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
* @func int64 ServerFilesDao::getIncomingStatsCount
* @return int64 c
* @sql
*       SELECT COUNT(*) AS c
*       FROM files_incoming_stat
*/
ServerFilesDao::CondInt64 ServerFilesDao::getIncomingStatsCount(void)
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
		ret.value=watoi64(res[0]["c"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerFilesDao::delIncomingStatEntry
* @sql
*       DELETE FROM files_incoming_stat WHERE id=:id(int64)
*/
void ServerFilesDao::delIncomingStatEntry(int64 id)
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
* @func vector<SIncomingStat> ServerFilesDao::getIncomingStats
* @return int64 id, int64 filesize, int clientid, int backupid, string existing_clients, int direction, int incremental
* @sql
*       SELECT id, filesize, clientid, backupid, existing_clients, direction, incremental
*       FROM files_incoming_stat LIMIT 10000
*/
std::vector<ServerFilesDao::SIncomingStat> ServerFilesDao::getIncomingStats(void)
{
	if(q_getIncomingStats==NULL)
	{
		q_getIncomingStats=db->Prepare("SELECT id, filesize, clientid, backupid, existing_clients, direction, incremental FROM files_incoming_stat LIMIT 10000", false);
	}
	db_results res=q_getIncomingStats->Read();
	std::vector<ServerFilesDao::SIncomingStat> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i]["id"]);
		ret[i].filesize=watoi64(res[i]["filesize"]);
		ret[i].clientid=watoi(res[i]["clientid"]);
		ret[i].backupid=watoi(res[i]["backupid"]);
		ret[i].existing_clients=res[i]["existing_clients"];
		ret[i].direction=watoi(res[i]["direction"]);
		ret[i].incremental=watoi(res[i]["incremental"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerFilesDao::deleteFiles
* @sql
*	DELETE FROM files WHERE backupid=:backupid(int)
*/
void ServerFilesDao::deleteFiles(int backupid)
{
	if(q_deleteFiles==NULL)
	{
		q_deleteFiles=db->Prepare("DELETE FROM files WHERE backupid=?", false);
	}
	q_deleteFiles->Bind(backupid);
	q_deleteFiles->Write();
	q_deleteFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerFilesDao::removeDanglingFiles
* @sql
*	DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)
*/
void ServerFilesDao::removeDanglingFiles(void)
{
	if(q_removeDanglingFiles==NULL)
	{
		q_removeDanglingFiles=db->Prepare("DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)", false);
	}
	q_removeDanglingFiles->Write();
}

int64 ServerFilesDao::addFileEntryExternal(int backupid, const std::string& fullpath, const std::string& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to)
{
	addFileEntry(backupid, fullpath, hashpath, shahash, filesize, rsize, clientid, incremental, next_entry, prev_entry, pointed_to);

	int64 id = db->getLastInsertID();

	if (prev_entry != 0)
	{
		setNextEntry(id, prev_entry);
	}

	if (next_entry != 0)
	{
		setPrevEntry(id, next_entry);
	}

	return id;
}

//@-SQLGenSetup
void ServerFilesDao::prepareQueries()
{
	q_setNextEntry=NULL;
	q_setPrevEntry=NULL;
	q_setPointedTo=NULL;
	q_getPointedTo=NULL;
	q_delFileEntry=NULL;
	q_getFileEntry=NULL;
	q_getStatFileEntry=NULL;
	q_addFileEntry=NULL;
	q_createTemporaryPathLookupTable=NULL;
	q_dropTemporaryPathLookupTable=NULL;
	q_dropTemporaryPathLookupIndex=NULL;
	q_populateTemporaryPathLookupTable=NULL;
	q_createTemporaryPathLookupIndex=NULL;
	q_lookupEntryIdByPath=NULL;
	q_addIncomingFile=NULL;
	q_getIncomingStatsCount=NULL;
	q_delIncomingStatEntry=NULL;
	q_getIncomingStats=NULL;
	q_deleteFiles=NULL;
	q_removeDanglingFiles=NULL;
}

//@-SQLGenDestruction
void ServerFilesDao::destroyQueries()
{
	db->destroyQuery(q_setNextEntry);
	db->destroyQuery(q_setPrevEntry);
	db->destroyQuery(q_setPointedTo);
	db->destroyQuery(q_getPointedTo);
	db->destroyQuery(q_delFileEntry);
	db->destroyQuery(q_getFileEntry);
	db->destroyQuery(q_getStatFileEntry);
	db->destroyQuery(q_addFileEntry);
	db->destroyQuery(q_createTemporaryPathLookupTable);
	db->destroyQuery(q_dropTemporaryPathLookupTable);
	db->destroyQuery(q_dropTemporaryPathLookupIndex);
	db->destroyQuery(q_populateTemporaryPathLookupTable);
	db->destroyQuery(q_createTemporaryPathLookupIndex);
	db->destroyQuery(q_lookupEntryIdByPath);
	db->destroyQuery(q_addIncomingFile);
	db->destroyQuery(q_getIncomingStatsCount);
	db->destroyQuery(q_delIncomingStatEntry);
	db->destroyQuery(q_getIncomingStats);
	db->destroyQuery(q_deleteFiles);
	db->destroyQuery(q_removeDanglingFiles);
}
