/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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

#include "KvStoreDao.h"
#include "../stringtools.h"
#include <memory.h>
#include <assert.h>

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_transactions
*        (id INTEGER PRIMARY KEY AUTOINCREMENT,
*         completed INTEGER DEFAULT 0,
*         active INTEGER DEFAULT 1,
*		  mirrored INTEGER DEFAULT 0)
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_transactions_cd
*        (id INTEGER PRIMARY KEY AUTOINCREMENT,
*		  cd_id INTEGER,
*         completed INTEGER DEFAULT 0,
*         active INTEGER DEFAULT 1,
*		  mirrored INTEGER DEFAULT 0)
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_objects (
*				trans_id INTEGER,
*				tkey BLOB,
*				size INTEGER,
*				md5sum BLOB,
*				last_modified INTEGER,
*				mirrored INTEGER DEFAULT 0,
*				PRIMARY KEY(tkey, trans_id) )
*/

/**
* TODO: On put, iterate over old/new tkey and put them into this table:
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_objects_del_old (
*				del_trans_id INTEGER,
*				old_trans_id INTEGER,
*				tkey BLOB,
*				PRIMARY KEY(del_trans_id, old_trans_id, tkey) )
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_objects_cd (
*				cd_id INTEGER,
*				trans_id INTEGER,
*				tkey BLOB,
*				size INTEGER,
*				md5sum BLOB,
*				last_modified INTEGER,
*				mirrored INTEGER DEFAULT 0,
*				PRIMARY KEY(cd_id, tkey, trans_id) )
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE INDEX IF NOT EXISTS clouddrive_objects_last_modified
*			ON clouddrive_objects(last_modified)
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE INDEX IF NOT EXISTS clouddrive_objects_cd_last_modified
*			ON clouddrive_objects_cd(last_modified)
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_generation (
*			generation INTEGER )
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS clouddrive_generation_cd (
*			cd_id INTEGER PRIMARY KEY, generation INTEGER )
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS tasks (
*			id INTEGER PRIMARY KEY AUTOINCREMENT,
*			task_id INTEGER,
*			trans_id INTEGER,
*			cd_id INTEGER DEFAULT 0,
*			active INTEGER DEFAULT 0,
*			created INTEGER)
*/

/**
* @-SQLGenTempSetup
* @sql
*		CREATE TABLE IF NOT EXISTS misc (
*			key TEXT PRIMARY KEY,
*			value TEXT )
*/

KvStoreDao::KvStoreDao( IDatabase *db )
	: db(db)
{
	prepareQueries();
}

//@-SQLGenSetup
void KvStoreDao::prepareQueries()
{
	q_createTransactionTable=nullptr;
	q_createTransactionTableCd=nullptr;
	q_createObjectTable=nullptr;
	q_createObjectTableCd=nullptr;
	q_createObjectTransIdIdx=nullptr;
	q_createObjectCdTransIdIdx=nullptr;
	q_createObjectLastModifiedIdx=nullptr;
	q_createObjectCdLastModifiedIdx=nullptr;
	q_dropObjectLastModifiedIdx=nullptr;
	q_dropObjectCdLastModifiedIdx=nullptr;
	q_createGenerationTable=nullptr;
	q_createGenerationTableCd=nullptr;
	q_createTaskTable=nullptr;
	q_createMiscTable=nullptr;
	q_setTaskActive=nullptr;
	q_getActiveTask=nullptr;
	q_getTasks=nullptr;
	q_getTask=nullptr;
	q_removeTask=nullptr;
	q_addTask=nullptr;
	q_getTransactionIds=nullptr;
	q_getTransactionIdsCd=nullptr;
	q_getSize=nullptr;
	q_getSizePartial=nullptr;
	q_getSizePartialLMInit=nullptr;
	q_getSizePartialLM=nullptr;
	q_setTransactionActive=nullptr;
	q_setTransactionActiveCd=nullptr;
	q_getMaxCompleteTransaction=nullptr;
	q_getMaxCompleteTransactionCd=nullptr;
	q_getIncompleteTransactions=nullptr;
	q_getIncompleteTransactionsCd=nullptr;
	q_deleteTransaction=nullptr;
	q_deleteTransactionCd=nullptr;
	q_getTransactionObjectsMd5=nullptr;
	q_getTransactionObjectsMd5Cd=nullptr;
	q_getTransactionObjects=nullptr;
	q_getTransactionObjectsCd=nullptr;
	q_deleteTransactionObjects=nullptr;
	q_deleteTransactionObjectsCd=nullptr;
	q_newTransaction=nullptr;
	q_newTransactionCd=nullptr;
	q_insertTransaction=nullptr;
	q_insertTransactionCd=nullptr;
	q_setTransactionComplete=nullptr;
	q_setTransactionCompleteCd=nullptr;
	q_getDeletableTransactions=nullptr;
	q_getDeletableTransactionsCd=nullptr;
	q_getLastFinalizedTransactions=nullptr;
	q_getLastFinalizedTransactionsCd=nullptr;
	q_getDeletableObjectsMd5Ordered=nullptr;
	q_getDeletableObjectsMd5Cd=nullptr;
	q_getDeletableObjectsMd5=nullptr;
	q_getDeletableObjectsOrdered=nullptr;
	q_getDeletableObjects=nullptr;
	q_deleteDeletableObjects=nullptr;
	q_deleteDeletableObjectsCd=nullptr;
	q_addDelMarkerObject=nullptr;
	q_addDelMarkerObjectCd=nullptr;
	q_addObject=nullptr;
	q_addObjectCd=nullptr;
	q_addObject2=nullptr;
	q_addObject2Cd=nullptr;
	q_addPartialObject=nullptr;
	q_addPartialObjectCd=nullptr;
	q_updateObjectSearch=nullptr;
	q_updateObject=nullptr;
	q_updateObjectCd=nullptr;
	q_updateObject2Cd=nullptr;
	q_updateObject2=nullptr;
	q_deletePartialObject=nullptr;
	q_deletePartialObjectCd=nullptr;
	q_updateGeneration=nullptr;
	q_getGeneration=nullptr;
	q_getGenerationCd=nullptr;
	q_insertGeneration=nullptr;
	q_getObjectInTransid=nullptr;
	q_getObjectInTransidCd=nullptr;
	q_getSingleObject=nullptr;
	q_getObject=nullptr;
	q_getObjectCd=nullptr;
	q_isTransactionActive=nullptr;
	q_isTransactionActiveCd=nullptr;
	q_deleteObject=nullptr;
	q_getMiscValue=nullptr;
	q_setMiscValue=nullptr;
	q_getTransactionProperties=nullptr;
	q_getTransactionPropertiesCd=nullptr;
	q_getInitialObjectsLM=nullptr;
	q_getInitialObjects=nullptr;
	q_getIterObjectsLMInit=nullptr;
	q_getIterObjectsLM=nullptr;
	q_getIterObjects=nullptr;
	q_updateObjectMd5sum=nullptr;
	q_updateObjectMd5sumCd=nullptr;
	q_insertAllDeletionTasks=nullptr;
	q_getUnmirroredObjects=nullptr;
	q_getUnmirroredObjectsSize=nullptr;
	q_setObjectMirrored=nullptr;
	q_getUnmirroredTransactions=nullptr;
	q_setTransactionMirrored=nullptr;
	q_updateGenerationCd=nullptr;
	q_getLowerTransidObject=nullptr;
	q_getLowerTransidObjectCd=nullptr;
}

//@-SQLGenDestruction
KvStoreDao::~KvStoreDao()
{
	db->destroyQuery(q_createTransactionTable);
	db->destroyQuery(q_createTransactionTableCd);
	db->destroyQuery(q_createObjectTable);
	db->destroyQuery(q_createObjectTableCd);
	db->destroyQuery(q_createObjectTransIdIdx);
	db->destroyQuery(q_createObjectCdTransIdIdx);
	db->destroyQuery(q_createObjectLastModifiedIdx);
	db->destroyQuery(q_createObjectCdLastModifiedIdx);
	db->destroyQuery(q_dropObjectLastModifiedIdx);
	db->destroyQuery(q_dropObjectCdLastModifiedIdx);
	db->destroyQuery(q_createGenerationTable);
	db->destroyQuery(q_createGenerationTableCd);
	db->destroyQuery(q_createTaskTable);
	db->destroyQuery(q_createMiscTable);
	db->destroyQuery(q_setTaskActive);
	db->destroyQuery(q_getActiveTask);
	db->destroyQuery(q_getTasks);
	db->destroyQuery(q_getTask);
	db->destroyQuery(q_removeTask);
	db->destroyQuery(q_addTask);
	db->destroyQuery(q_getTransactionIds);
	db->destroyQuery(q_getTransactionIdsCd);
	db->destroyQuery(q_getSize);
	db->destroyQuery(q_getSizePartial);
	db->destroyQuery(q_getSizePartialLMInit);
	db->destroyQuery(q_getSizePartialLM);
	db->destroyQuery(q_setTransactionActive);
	db->destroyQuery(q_setTransactionActiveCd);
	db->destroyQuery(q_getMaxCompleteTransaction);
	db->destroyQuery(q_getMaxCompleteTransactionCd);
	db->destroyQuery(q_getIncompleteTransactions);
	db->destroyQuery(q_getIncompleteTransactionsCd);
	db->destroyQuery(q_deleteTransaction);
	db->destroyQuery(q_deleteTransactionCd);
	db->destroyQuery(q_getTransactionObjectsMd5);
	db->destroyQuery(q_getTransactionObjectsMd5Cd);
	db->destroyQuery(q_getTransactionObjects);
	db->destroyQuery(q_getTransactionObjectsCd);
	db->destroyQuery(q_deleteTransactionObjects);
	db->destroyQuery(q_deleteTransactionObjectsCd);
	db->destroyQuery(q_newTransaction);
	db->destroyQuery(q_newTransactionCd);
	db->destroyQuery(q_insertTransaction);
	db->destroyQuery(q_insertTransactionCd);
	db->destroyQuery(q_setTransactionComplete);
	db->destroyQuery(q_setTransactionCompleteCd);
	db->destroyQuery(q_getDeletableTransactions);
	db->destroyQuery(q_getDeletableTransactionsCd);
	db->destroyQuery(q_getLastFinalizedTransactions);
	db->destroyQuery(q_getLastFinalizedTransactionsCd);
	db->destroyQuery(q_getDeletableObjectsMd5Ordered);
	db->destroyQuery(q_getDeletableObjectsMd5Cd);
	db->destroyQuery(q_getDeletableObjectsMd5);
	db->destroyQuery(q_getDeletableObjectsOrdered);
	db->destroyQuery(q_getDeletableObjects);
	db->destroyQuery(q_deleteDeletableObjects);
	db->destroyQuery(q_deleteDeletableObjectsCd);
	db->destroyQuery(q_addDelMarkerObject);
	db->destroyQuery(q_addDelMarkerObjectCd);
	db->destroyQuery(q_addObject);
	db->destroyQuery(q_addObjectCd);
	db->destroyQuery(q_addObject2);
	db->destroyQuery(q_addObject2Cd);
	db->destroyQuery(q_addPartialObject);
	db->destroyQuery(q_addPartialObjectCd);
	db->destroyQuery(q_updateObjectSearch);
	db->destroyQuery(q_updateObject);
	db->destroyQuery(q_updateObjectCd);
	db->destroyQuery(q_updateObject2Cd);
	db->destroyQuery(q_updateObject2);
	db->destroyQuery(q_deletePartialObject);
	db->destroyQuery(q_deletePartialObjectCd);
	db->destroyQuery(q_updateGeneration);
	db->destroyQuery(q_getGeneration);
	db->destroyQuery(q_getGenerationCd);
	db->destroyQuery(q_insertGeneration);
	db->destroyQuery(q_getObjectInTransid);
	db->destroyQuery(q_getObjectInTransidCd);
	db->destroyQuery(q_getSingleObject);
	db->destroyQuery(q_getObject);
	db->destroyQuery(q_getObjectCd);
	db->destroyQuery(q_isTransactionActive);
	db->destroyQuery(q_isTransactionActiveCd);
	db->destroyQuery(q_deleteObject);
	db->destroyQuery(q_getMiscValue);
	db->destroyQuery(q_setMiscValue);
	db->destroyQuery(q_getTransactionProperties);
	db->destroyQuery(q_getTransactionPropertiesCd);
	db->destroyQuery(q_getInitialObjectsLM);
	db->destroyQuery(q_getInitialObjects);
	db->destroyQuery(q_getIterObjectsLMInit);
	db->destroyQuery(q_getIterObjectsLM);
	db->destroyQuery(q_getIterObjects);
	db->destroyQuery(q_updateObjectMd5sum);
	db->destroyQuery(q_updateObjectMd5sumCd);
	db->destroyQuery(q_insertAllDeletionTasks);
	db->destroyQuery(q_getUnmirroredObjects);
	db->destroyQuery(q_getUnmirroredObjectsSize);
	db->destroyQuery(q_setObjectMirrored);
	db->destroyQuery(q_getUnmirroredTransactions);
	db->destroyQuery(q_setTransactionMirrored);
	db->destroyQuery(q_updateGenerationCd);
	db->destroyQuery(q_getLowerTransidObject);
	db->destroyQuery(q_getLowerTransidObjectCd);
}

IDatabase * KvStoreDao::getDb()
{
	return db;
}

void KvStoreDao::createTables()
{
	createTransactionTable();
	createTaskTable();
	createObjectTable();
	createObjectTransIdIdx();
	createGenerationTable();
	createMiscTable();

	createObjectTableCd();
	createTransactionTableCd();
	createObjectCdTransIdIdx();
	createGenerationTableCd();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createTransactionTable
* @sql
*	CREATE TABLE IF NOT EXISTS clouddrive_transactions
*        (id INTEGER PRIMARY KEY,
*         completed INTEGER DEFAULT 0,
*         active INTEGER DEFAULT 1)
*/
void KvStoreDao::createTransactionTable(void)
{
	if(q_createTransactionTable==nullptr)
	{
		q_createTransactionTable=db->Prepare("CREATE TABLE IF NOT EXISTS clouddrive_transactions (id INTEGER PRIMARY KEY, completed INTEGER DEFAULT 0, active INTEGER DEFAULT 1)", false);
	}
	q_createTransactionTable->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createTransactionTableCd
* @sql
*	CREATE TABLE IF NOT EXISTS clouddrive_transactions_cd
*        (id INTEGER PRIMARY KEY,
*		  cd_id INTEGER,
*         completed INTEGER DEFAULT 0,
*         active INTEGER DEFAULT 1)
*/
void KvStoreDao::createTransactionTableCd(void)
{
	if(q_createTransactionTableCd==nullptr)
	{
		q_createTransactionTableCd=db->Prepare("CREATE TABLE IF NOT EXISTS clouddrive_transactions_cd (id INTEGER PRIMARY KEY, cd_id INTEGER, completed INTEGER DEFAULT 0, active INTEGER DEFAULT 1)", false);
	}
	q_createTransactionTableCd->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createObjectTable
* @sql
*	CREATE TABLE IF NOT EXISTS clouddrive_objects (
*				trans_id INTEGER,
*				tkey BLOB,
*				size INTEGER,
*				md5sum BLOB,
*				PRIMARY KEY(tkey, trans_id) )
*/
void KvStoreDao::createObjectTable(void)
{
	if(q_createObjectTable==nullptr)
	{
		q_createObjectTable=db->Prepare("CREATE TABLE IF NOT EXISTS clouddrive_objects ( trans_id INTEGER, tkey BLOB, size INTEGER, md5sum BLOB, PRIMARY KEY(tkey, trans_id) )", false);
	}
	q_createObjectTable->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createObjectTableCd
* @sql
*	CREATE TABLE IF NOT EXISTS clouddrive_objects_cd (
*				cd_id INTEGER,
*				trans_id INTEGER,
*				tkey BLOB,
*				size INTEGER,
*				md5sum BLOB,
*				PRIMARY KEY(cd_id, tkey, trans_id) )
*/
void KvStoreDao::createObjectTableCd(void)
{
	if(q_createObjectTableCd==nullptr)
	{
		q_createObjectTableCd=db->Prepare("CREATE TABLE IF NOT EXISTS clouddrive_objects_cd ( cd_id INTEGER, trans_id INTEGER, tkey BLOB, size INTEGER, md5sum BLOB, PRIMARY KEY(cd_id, tkey, trans_id) )", false);
	}
	q_createObjectTableCd->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createObjectTransIdIdx
* @sql
*	CREATE INDEX IF NOT EXISTS clouddrive_objects_trans_id_idx 
*		ON clouddrive_objects(trans_id)
*/
void KvStoreDao::createObjectTransIdIdx(void)
{
	if(q_createObjectTransIdIdx==nullptr)
	{
		q_createObjectTransIdIdx=db->Prepare("CREATE INDEX IF NOT EXISTS clouddrive_objects_trans_id_idx  ON clouddrive_objects(trans_id)", false);
	}
	q_createObjectTransIdIdx->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createObjectCdTransIdIdx
* @sql
*	CREATE INDEX IF NOT EXISTS clouddrive_objects_cd_trans_id_idx
*		ON clouddrive_objects_cd(cd_id, trans_id)
*/
void KvStoreDao::createObjectCdTransIdIdx(void)
{
	if(q_createObjectCdTransIdIdx==nullptr)
	{
		q_createObjectCdTransIdIdx=db->Prepare("CREATE INDEX IF NOT EXISTS clouddrive_objects_cd_trans_id_idx ON clouddrive_objects_cd(cd_id, trans_id)", false);
	}
	q_createObjectCdTransIdIdx->Write();
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::createObjectLastModifiedIdx
* @sql
*	CREATE INDEX IF NOT EXISTS clouddrive_objects_last_modified
*		ON clouddrive_objects(last_modified)
*/
bool KvStoreDao::createObjectLastModifiedIdx(void)
{
	if(q_createObjectLastModifiedIdx==nullptr)
	{
		q_createObjectLastModifiedIdx=db->Prepare("CREATE INDEX IF NOT EXISTS clouddrive_objects_last_modified ON clouddrive_objects(last_modified)", false);
	}
	bool ret = q_createObjectLastModifiedIdx->Write();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::createObjectCdLastModifiedIdx
* @sql
*	CREATE INDEX IF NOT EXISTS clouddrive_objects_cd_last_modified
*		ON clouddrive_objects_cd(last_modified)
*/
bool KvStoreDao::createObjectCdLastModifiedIdx(void)
{
	if(q_createObjectCdLastModifiedIdx==nullptr)
	{
		q_createObjectCdLastModifiedIdx=db->Prepare("CREATE INDEX IF NOT EXISTS clouddrive_objects_cd_last_modified ON clouddrive_objects_cd(last_modified)", false);
	}
	bool ret = q_createObjectCdLastModifiedIdx->Write();
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::dropObjectLastModifiedIdx
* @sql
*	DROP INDEX clouddrive_objects_last_modified
*/
void KvStoreDao::dropObjectLastModifiedIdx(void)
{
	if(q_dropObjectLastModifiedIdx==nullptr)
	{
		q_dropObjectLastModifiedIdx=db->Prepare("DROP INDEX clouddrive_objects_last_modified", false);
	}
	q_dropObjectLastModifiedIdx->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::dropObjectCdLastModifiedIdx
* @sql
*	DROP INDEX clouddrive_objects_cd_last_modified
*/
void KvStoreDao::dropObjectCdLastModifiedIdx(void)
{
	if(q_dropObjectCdLastModifiedIdx==nullptr)
	{
		q_dropObjectCdLastModifiedIdx=db->Prepare("DROP INDEX clouddrive_objects_cd_last_modified", false);
	}
	q_dropObjectCdLastModifiedIdx->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createGenerationTable
* @sql
*	CREATE TABLE IF NOT EXISTS clouddrive_generation (
*			generation INTEGER )
*/
void KvStoreDao::createGenerationTable(void)
{
	if(q_createGenerationTable==nullptr)
	{
		q_createGenerationTable=db->Prepare("CREATE TABLE IF NOT EXISTS clouddrive_generation ( generation INTEGER )", false);
	}
	q_createGenerationTable->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createGenerationTableCd
* @sql
*	CREATE TABLE IF NOT EXISTS clouddrive_generation_cd (
*			cd_id INTEGER PRIMARY KEY, generation INTEGER )
*/
void KvStoreDao::createGenerationTableCd(void)
{
	if(q_createGenerationTableCd==nullptr)
	{
		q_createGenerationTableCd=db->Prepare("CREATE TABLE IF NOT EXISTS clouddrive_generation_cd ( cd_id INTEGER PRIMARY KEY, generation INTEGER )", false);
	}
	q_createGenerationTableCd->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createTaskTable
* @sql
*	CREATE TABLE IF NOT EXISTS tasks (
*			id INTEGER PRIMARY KEY AUTOINCREMENT,
*			task_id INTEGER,
*			trans_id INTEGER,
*			cd_id INTEGER DEFAULT 0,
*			active INTEGER DEFAULT 0)
*/
void KvStoreDao::createTaskTable(void)
{
	if(q_createTaskTable==nullptr)
	{
		q_createTaskTable=db->Prepare("CREATE TABLE IF NOT EXISTS tasks ( id INTEGER PRIMARY KEY AUTOINCREMENT, task_id INTEGER, trans_id INTEGER, cd_id INTEGER DEFAULT 0, active INTEGER DEFAULT 0)", false);
	}
	q_createTaskTable->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::createMiscTable
* @sql
*	CREATE TABLE IF NOT EXISTS misc (
*			key TEXT PRIMARY KEY,
*			value TEXT )
*/
void KvStoreDao::createMiscTable(void)
{
	if(q_createMiscTable==nullptr)
	{
		q_createMiscTable=db->Prepare("CREATE TABLE IF NOT EXISTS misc ( key TEXT PRIMARY KEY, value TEXT )", false);
	}
	q_createMiscTable->Write();
}



/**
* @-SQLGenAccess
* @func void KvStoreDao::setTaskActive
* @sql
*	UPDATE tasks SET active=1 WHERE id=:id(int64)
*/
void KvStoreDao::setTaskActive(int64 id)
{
	if(q_setTaskActive==nullptr)
	{
		q_setTaskActive=db->Prepare("UPDATE tasks SET active=1 WHERE id=?", false);
	}
	q_setTaskActive->Bind(id);
	q_setTaskActive->Write();
	q_setTaskActive->Reset();
}

/**
* @-SQLGenAccess
* @func Task KvStoreDao::getActiveTask
* @return int64 id, int task_id, int64 trans_id, int64 cd_id
* @sql
*	SELECT id, task_id, trans_id, cd_id FROM tasks WHERE active!=0 ORDER BY id ASC LIMIT 1
*/
KvStoreDao::Task KvStoreDao::getActiveTask(void)
{
	if(q_getActiveTask==nullptr)
	{
		q_getActiveTask=db->Prepare("SELECT id, task_id, trans_id, cd_id FROM tasks WHERE active!=0 ORDER BY id ASC LIMIT 1", false);
	}
	db_results res=q_getActiveTask->Read();
	Task ret = { false, 0, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.task_id=watoi(res[0]["task_id"]);
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.cd_id=watoi64(res[0]["cd_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<Task> KvStoreDao::getTasks
* @return int64 id, int task_id, int64 trans_id, int64 cd_id
* @sql
*	SELECT id, task_id, trans_id, cd_id FROM tasks WHERE (created<=:created_max(int64) OR created IS NULL)
*		AND task_id=:task_id(int) AND cd_id=:cd_id(int64) ORDER BY id ASC
*/
std::vector<KvStoreDao::Task> KvStoreDao::getTasks(int64 created_max, int task_id, int64 cd_id)
{
	if(q_getTasks==nullptr)
	{
		q_getTasks=db->Prepare("SELECT id, task_id, trans_id, cd_id FROM tasks WHERE (created<=? OR created IS NULL) AND task_id=? AND cd_id=? ORDER BY id ASC", false);
	}
	q_getTasks->Bind(created_max);
	q_getTasks->Bind(task_id);
	q_getTasks->Bind(cd_id);
	db_results res=q_getTasks->Read();
	q_getTasks->Reset();
	std::vector<KvStoreDao::Task> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi64(res[i]["id"]);
		ret[i].task_id=watoi(res[i]["task_id"]);
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].cd_id=watoi64(res[i]["cd_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func Task KvStoreDao::getTask
* @return int64 id, int task_id, int64 trans_id, int64 cd_id
* @sql
*	SELECT id, task_id, trans_id, cd_id FROM tasks WHERE created<=:created_max(int64) OR created IS NULL ORDER BY id ASC LIMIT 1
*/
KvStoreDao::Task KvStoreDao::getTask(int64 created_max)
{
	if(q_getTask==nullptr)
	{
		q_getTask=db->Prepare("SELECT id, task_id, trans_id, cd_id FROM tasks WHERE created<=? OR created IS NULL ORDER BY id ASC LIMIT 1", false);
	}
	q_getTask->Bind(created_max);
	db_results res=q_getTask->Read();
	q_getTask->Reset();
	Task ret = { false, 0, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi64(res[0]["id"]);
		ret.task_id=watoi(res[0]["task_id"]);
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.cd_id=watoi64(res[0]["cd_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::removeTask
* @sql
*	DELETE FROM tasks WHERE id=:id(int64)
*/
void KvStoreDao::removeTask(int64 id)
{
	if(q_removeTask==nullptr)
	{
		q_removeTask=db->Prepare("DELETE FROM tasks WHERE id=?", false);
	}
	q_removeTask->Bind(id);
	q_removeTask->Write();
	q_removeTask->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::addTask
* @sql
*	INSERT INTO tasks (task_id, trans_id, created, cd_id) VALUES (:task_id(int), :trans_id(int64), :created(int64), :cd_id(int64) )
*/
void KvStoreDao::addTask(int task_id, int64 trans_id, int64 created, int64 cd_id)
{
	if(q_addTask==nullptr)
	{
		q_addTask=db->Prepare("INSERT INTO tasks (task_id, trans_id, created, cd_id) VALUES (?, ?, ?, ? )", false);
	}
	q_addTask->Bind(task_id);
	q_addTask->Bind(trans_id);
	q_addTask->Bind(created);
	q_addTask->Bind(cd_id);
	q_addTask->Write();
	q_addTask->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SCdTrans> KvStoreDao::getTransactionIds
* @return int64 id, int completed, int active
* @sql
*	SELECT id, completed, active FROM clouddrive_transactions
*/
std::vector<KvStoreDao::SCdTrans> KvStoreDao::getTransactionIds(void)
{
	if(q_getTransactionIds==nullptr)
	{
		q_getTransactionIds=db->Prepare("SELECT id, completed, active FROM clouddrive_transactions", false);
	}
	db_results res=q_getTransactionIds->Read();
	std::vector<KvStoreDao::SCdTrans> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i]["id"]);
		ret[i].completed=watoi(res[i]["completed"]);
		ret[i].active=watoi(res[i]["active"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SCdTrans> KvStoreDao::getTransactionIdsCd
* @return int64 id, int completed, int active
* @sql
*	SELECT id, completed, active FROM clouddrive_transactions_cd WHERE cd_id=:cd_id(int64)
*/
std::vector<KvStoreDao::SCdTrans> KvStoreDao::getTransactionIdsCd(int64 cd_id)
{
	if(q_getTransactionIdsCd==nullptr)
	{
		q_getTransactionIdsCd=db->Prepare("SELECT id, completed, active FROM clouddrive_transactions_cd WHERE cd_id=?", false);
	}
	q_getTransactionIdsCd->Bind(cd_id);
	db_results res=q_getTransactionIdsCd->Read();
	q_getTransactionIdsCd->Reset();
	std::vector<KvStoreDao::SCdTrans> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i]["id"]);
		ret[i].completed=watoi(res[i]["completed"]);
		ret[i].active=watoi(res[i]["active"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SSize KvStoreDao::getSize
* @return int64 size, int64 count
* @sql
*	SELECT SUM(size) AS size, COUNT(size) AS count FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!= -1 AND active!=0
*/
KvStoreDao::SSize KvStoreDao::getSize(void)
{
	if(q_getSize==nullptr)
	{
		q_getSize=db->Prepare("SELECT SUM(size) AS size, COUNT(size) AS count FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!= -1 AND active!=0", false);
	}
	db_results res=q_getSize->Read();
	SSize ret = { false, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.size=watoi64(res[0]["size"]);
		ret.count=watoi64(res[0]["count"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getSizePartial
* @return int64_raw size
* @sql
*	SELECT SUM(size) AS size FROM clouddrive_objects WHERE size!= -1 AND tkey>:tkey(blob) OR(tkey = :tkey(blob) AND trans_id>:tans_id(int64))
*/
int64 KvStoreDao::getSizePartial(const std::string& tkey, int64 tans_id)
{
	if(q_getSizePartial==nullptr)
	{
		q_getSizePartial=db->Prepare("SELECT SUM(size) AS size FROM clouddrive_objects WHERE size!= -1 AND tkey>? OR(tkey = ? AND trans_id>?)", false);
	}
	q_getSizePartial->Bind(tkey.c_str(), (_u32)tkey.size());
	q_getSizePartial->Bind(tkey.c_str(), (_u32)tkey.size());
	q_getSizePartial->Bind(tans_id);
	db_results res=q_getSizePartial->Read();
	q_getSizePartial->Reset();
	assert(!res.empty());
	return watoi64(res[0]["size"]);
}


/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getSizePartialLMInit
* @return int64_raw size
* @sql
*	SELECT SUM(size) AS size FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!= -1 AND last_modified>=:last_modified_start(int64) AND active!=0
*/
int64 KvStoreDao::getSizePartialLMInit(int64 last_modified_start)
{
	if(q_getSizePartialLMInit==nullptr)
	{
		q_getSizePartialLMInit=db->Prepare("SELECT SUM(size) AS size FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!= -1 AND last_modified>=? AND active!=0", false);
	}
	q_getSizePartialLMInit->Bind(last_modified_start);
	db_results res=q_getSizePartialLMInit->Read();
	q_getSizePartialLMInit->Reset();
	assert(!res.empty());
	return watoi64(res[0]["size"]);
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getSizePartialLM
* @return int64_raw size
* @sql
*	SELECT SUM(size) AS size FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!= -1 AND last_modified>:last_modified_start(int64) AND last_modified<:last_modified_stop(int64) AND active!=0
*/
int64 KvStoreDao::getSizePartialLM(int64 last_modified_start, int64 last_modified_stop)
{
	if(q_getSizePartialLM==nullptr)
	{
		q_getSizePartialLM=db->Prepare("SELECT SUM(size) AS size FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!= -1 AND last_modified>? AND last_modified<? AND active!=0", false);
	}
	q_getSizePartialLM->Bind(last_modified_start);
	q_getSizePartialLM->Bind(last_modified_stop);
	db_results res=q_getSizePartialLM->Read();
	q_getSizePartialLM->Reset();
	assert(!res.empty());
	return watoi64(res[0]["size"]);
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::setTransactionActive
* @sql
*	UPDATE clouddrive_transactions SET active=:active(int) WHERE id=:id(int64)
*/
void KvStoreDao::setTransactionActive(int active, int64 id)
{
	if(q_setTransactionActive==nullptr)
	{
		q_setTransactionActive=db->Prepare("UPDATE clouddrive_transactions SET active=? WHERE id=?", false);
	}
	q_setTransactionActive->Bind(active);
	q_setTransactionActive->Bind(id);
	q_setTransactionActive->Write();
	q_setTransactionActive->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::setTransactionActiveCd
* @sql
*	UPDATE clouddrive_transactions_cd SET active=:active(int) WHERE cd_id=:cd_id(int64) AND id=:id(int64)
*/
void KvStoreDao::setTransactionActiveCd(int active, int64 cd_id, int64 id)
{
	if(q_setTransactionActiveCd==nullptr)
	{
		q_setTransactionActiveCd=db->Prepare("UPDATE clouddrive_transactions_cd SET active=? WHERE cd_id=? AND id=?", false);
	}
	q_setTransactionActiveCd->Bind(active);
	q_setTransactionActiveCd->Bind(cd_id);
	q_setTransactionActiveCd->Bind(id);
	q_setTransactionActiveCd->Write();
	q_setTransactionActiveCd->Reset();
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getMaxCompleteTransaction
* @return int64 max_id
* @sql
*	SELECT MAX(id) AS max_id FROM clouddrive_transactions WHERE
*       completed=2
*/
KvStoreDao::CondInt64 KvStoreDao::getMaxCompleteTransaction(void)
{
	if(q_getMaxCompleteTransaction==nullptr)
	{
		q_getMaxCompleteTransaction=db->Prepare("SELECT MAX(id) AS max_id FROM clouddrive_transactions WHERE completed=2", false);
	}
	db_results res=q_getMaxCompleteTransaction->Read();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["max_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getMaxCompleteTransactionCd
* @return int64 max_id
* @sql
*	SELECT MAX(id) AS max_id FROM clouddrive_transactions_cd WHERE
*       completed=2 AND cd_id=:cd_id(int64)
*/
KvStoreDao::CondInt64 KvStoreDao::getMaxCompleteTransactionCd(int64 cd_id)
{
	if(q_getMaxCompleteTransactionCd==nullptr)
	{
		q_getMaxCompleteTransactionCd=db->Prepare("SELECT MAX(id) AS max_id FROM clouddrive_transactions_cd WHERE completed=2 AND cd_id=?", false);
	}
	q_getMaxCompleteTransactionCd->Bind(cd_id);
	db_results res=q_getMaxCompleteTransactionCd->Read();
	q_getMaxCompleteTransactionCd->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["max_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int64> KvStoreDao::getIncompleteTransactions
* @return int64 id
* @sql
*	SELECT id FROM clouddrive_transactions WHERE 
*       completed=0 OR ( completed=1 AND id>:max_active(int64) )
*/
std::vector<int64> KvStoreDao::getIncompleteTransactions(int64 max_active)
{
	if(q_getIncompleteTransactions==nullptr)
	{
		q_getIncompleteTransactions=db->Prepare("SELECT id FROM clouddrive_transactions WHERE  completed=0 OR ( completed=1 AND id>? )", false);
	}
	q_getIncompleteTransactions->Bind(max_active);
	db_results res=q_getIncompleteTransactions->Read();
	q_getIncompleteTransactions->Reset();
	std::vector<int64> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi64(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int64> KvStoreDao::getIncompleteTransactionsCd
* @return int64 id
* @sql
*	SELECT id FROM clouddrive_transactions_cd WHERE
*       completed=0 OR ( completed=1 AND id>:max_active(int64) ) AND cd_id=:cd_id(int64)
*/
std::vector<int64> KvStoreDao::getIncompleteTransactionsCd(int64 max_active, int64 cd_id)
{
	if(q_getIncompleteTransactionsCd==nullptr)
	{
		q_getIncompleteTransactionsCd=db->Prepare("SELECT id FROM clouddrive_transactions_cd WHERE completed=0 OR ( completed=1 AND id>? ) AND cd_id=?", false);
	}
	q_getIncompleteTransactionsCd->Bind(max_active);
	q_getIncompleteTransactionsCd->Bind(cd_id);
	db_results res=q_getIncompleteTransactionsCd->Read();
	q_getIncompleteTransactionsCd->Reset();
	std::vector<int64> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi64(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::deleteTransaction
* @sql
*   DELETE FROM clouddrive_transactions WHERE id=:id(int64)
*/
bool KvStoreDao::deleteTransaction(int64 id)
{
	if(q_deleteTransaction==nullptr)
	{
		q_deleteTransaction=db->Prepare("DELETE FROM clouddrive_transactions WHERE id=?", false);
	}
	q_deleteTransaction->Bind(id);
	bool ret = q_deleteTransaction->Write();
	q_deleteTransaction->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::deleteTransactionCd
* @sql
*   DELETE FROM clouddrive_transactions_cd WHERE cd_id=:cd_id(int64) AND id=:id(int64)
*/
bool KvStoreDao::deleteTransactionCd(int64 cd_id, int64 id)
{
	if(q_deleteTransactionCd==nullptr)
	{
		q_deleteTransactionCd=db->Prepare("DELETE FROM clouddrive_transactions_cd WHERE cd_id=? AND id=?", false);
	}
	q_deleteTransactionCd->Bind(cd_id);
	q_deleteTransactionCd->Bind(id);
	bool ret = q_deleteTransactionCd->Write();
	q_deleteTransactionCd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SDelItemMd5> KvStoreDao::getTransactionObjectsMd5
* @return blob tkey, string md5sum
* @sql
*   SELECT tkey, md5sum FROM clouddrive_objects WHERE
*          trans_id=:trans_id(int64) AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<KvStoreDao::SDelItemMd5> KvStoreDao::getTransactionObjectsMd5(int64 trans_id)
{
	if(q_getTransactionObjectsMd5==nullptr)
	{
		q_getTransactionObjectsMd5=db->Prepare("SELECT tkey, md5sum FROM clouddrive_objects WHERE trans_id=? AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getTransactionObjectsMd5->Bind(trans_id);
	db_results res=q_getTransactionObjectsMd5->Read();
	q_getTransactionObjectsMd5->Reset();
	std::vector<KvStoreDao::SDelItemMd5> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SDelItemMd5> KvStoreDao::getTransactionObjectsMd5Cd
* @return blob tkey, string md5sum
* @sql
*   SELECT tkey, md5sum FROM clouddrive_objects_cd WHERE
*          cd_id=:cd_id(int64) AND trans_id=:trans_id(int64) AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<KvStoreDao::SDelItemMd5> KvStoreDao::getTransactionObjectsMd5Cd(int64 cd_id, int64 trans_id)
{
	if(q_getTransactionObjectsMd5Cd==nullptr)
	{
		q_getTransactionObjectsMd5Cd=db->Prepare("SELECT tkey, md5sum FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id=? AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getTransactionObjectsMd5Cd->Bind(cd_id);
	q_getTransactionObjectsMd5Cd->Bind(trans_id);
	db_results res=q_getTransactionObjectsMd5Cd->Read();
	q_getTransactionObjectsMd5Cd->Reset();
	std::vector<KvStoreDao::SDelItemMd5> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<blob> KvStoreDao::getTransactionObjects
* @return blob tkey
* @sql
*   SELECT tkey FROM clouddrive_objects WHERE 
*          trans_id=:trans_id(int64) AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<std::string> KvStoreDao::getTransactionObjects(int64 trans_id)
{
	if(q_getTransactionObjects==nullptr)
	{
		q_getTransactionObjects=db->Prepare("SELECT tkey FROM clouddrive_objects WHERE  trans_id=? AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getTransactionObjects->Bind(trans_id);
	db_results res=q_getTransactionObjects->Read();
	q_getTransactionObjects->Reset();
	std::vector<std::string> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i]["tkey"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<blob> KvStoreDao::getTransactionObjectsCd
* @return blob tkey
* @sql
*   SELECT tkey FROM clouddrive_objects_cd WHERE
*          cd_id=:cd_id(int64) AND trans_id=:trans_id(int64) AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<std::string> KvStoreDao::getTransactionObjectsCd(int64 cd_id, int64 trans_id)
{
	if(q_getTransactionObjectsCd==nullptr)
	{
		q_getTransactionObjectsCd=db->Prepare("SELECT tkey FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id=? AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getTransactionObjectsCd->Bind(cd_id);
	q_getTransactionObjectsCd->Bind(trans_id);
	db_results res=q_getTransactionObjectsCd->Read();
	q_getTransactionObjectsCd->Reset();
	std::vector<std::string> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=res[i]["tkey"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::deleteTransactionObjects
* @sql
*   DELETE FROM clouddrive_objects WHERE 
*          trans_id=:trans_id(int64)
*/
bool KvStoreDao::deleteTransactionObjects(int64 trans_id)
{
	if(q_deleteTransactionObjects==nullptr)
	{
		q_deleteTransactionObjects=db->Prepare("DELETE FROM clouddrive_objects WHERE  trans_id=?", false);
	}
	q_deleteTransactionObjects->Bind(trans_id);
	bool ret = q_deleteTransactionObjects->Write();
	q_deleteTransactionObjects->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::deleteTransactionObjectsCd
* @sql
*   DELETE FROM clouddrive_objects_cd WHERE
*          cd_id=:cd_id(int64) AND trans_id=:trans_id(int64)
*/
bool KvStoreDao::deleteTransactionObjectsCd(int64 cd_id, int64 trans_id)
{
	if(q_deleteTransactionObjectsCd==nullptr)
	{
		q_deleteTransactionObjectsCd=db->Prepare("DELETE FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id=?", false);
	}
	q_deleteTransactionObjectsCd->Bind(cd_id);
	q_deleteTransactionObjectsCd->Bind(trans_id);
	bool ret = q_deleteTransactionObjectsCd->Write();
	q_deleteTransactionObjectsCd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::newTransaction
* @sql
*   INSERT INTO clouddrive_transactions DEFAULT VALUES
*/
void KvStoreDao::newTransaction(void)
{
	if(q_newTransaction==nullptr)
	{
		q_newTransaction=db->Prepare("INSERT INTO clouddrive_transactions DEFAULT VALUES", false);
	}
	q_newTransaction->Write();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::newTransactionCd
* @sql
*   INSERT INTO clouddrive_transactions_cd (cd_id) VALUES (:cd_id(int64))
*/
void KvStoreDao::newTransactionCd(int64 cd_id)
{
	if(q_newTransactionCd==nullptr)
	{
		q_newTransactionCd=db->Prepare("INSERT INTO clouddrive_transactions_cd (cd_id) VALUES (?)", false);
	}
	q_newTransactionCd->Bind(cd_id);
	q_newTransactionCd->Write();
	q_newTransactionCd->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::insertTransaction
* @sql
*   INSERT OR REPLACE INTO clouddrive_transactions (id) VALUES (:id(int64))
*/
void KvStoreDao::insertTransaction(int64 id)
{
	if(q_insertTransaction==nullptr)
	{
		q_insertTransaction=db->Prepare("INSERT OR REPLACE INTO clouddrive_transactions (id) VALUES (?)", false);
	}
	q_insertTransaction->Bind(id);
	q_insertTransaction->Write();
	q_insertTransaction->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::insertTransactionCd
* @sql
*   INSERT OR REPLACE INTO clouddrive_transactions_cd (id, cd_id) VALUES (:id(int64), :cd_id(int64) )
*/
void KvStoreDao::insertTransactionCd(int64 id, int64 cd_id)
{
	if(q_insertTransactionCd==nullptr)
	{
		q_insertTransactionCd=db->Prepare("INSERT OR REPLACE INTO clouddrive_transactions_cd (id, cd_id) VALUES (?, ? )", false);
	}
	q_insertTransactionCd->Bind(id);
	q_insertTransactionCd->Bind(cd_id);
	q_insertTransactionCd->Write();
	q_insertTransactionCd->Reset();
}


/**
* @-SQLGenAccess
* @func void KvStoreDao::setTransactionComplete
* @sql
*   UPDATE clouddrive_transactions SET completed=:completed(int) WHERE id=:trans_id(int64)
*/
void KvStoreDao::setTransactionComplete(int completed, int64 trans_id)
{
	if(q_setTransactionComplete==nullptr)
	{
		q_setTransactionComplete=db->Prepare("UPDATE clouddrive_transactions SET completed=? WHERE id=?", false);
	}
	q_setTransactionComplete->Bind(completed);
	q_setTransactionComplete->Bind(trans_id);
	q_setTransactionComplete->Write();
	q_setTransactionComplete->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::setTransactionCompleteCd
* @sql
*   UPDATE clouddrive_transactions_cd SET completed=:completed(int) WHERE cd_id=:cd_id(int64) AND id=:trans_id(int64)
*/
void KvStoreDao::setTransactionCompleteCd(int completed, int64 cd_id, int64 trans_id)
{
	if(q_setTransactionCompleteCd==nullptr)
	{
		q_setTransactionCompleteCd=db->Prepare("UPDATE clouddrive_transactions_cd SET completed=? WHERE cd_id=? AND id=?", false);
	}
	q_setTransactionCompleteCd->Bind(completed);
	q_setTransactionCompleteCd->Bind(cd_id);
	q_setTransactionCompleteCd->Bind(trans_id);
	q_setTransactionCompleteCd->Write();
	q_setTransactionCompleteCd->Reset();
}

/**
* @-SQLGenAccess
* @func vector<int64> KvStoreDao::getDeletableTransactions
* @return int64 id
* @sql
*   SELECT id FROM clouddrive_transactions t WHERE
*       id<:curr_trans_id(int64) AND completed!=0 AND NOT EXISTS 
*          (SELECT * FROM clouddrive_objects WHERE trans_id=t.id)
*/
std::vector<int64> KvStoreDao::getDeletableTransactions(int64 curr_trans_id)
{
	if(q_getDeletableTransactions==nullptr)
	{
		q_getDeletableTransactions=db->Prepare("SELECT id FROM clouddrive_transactions t WHERE id<? AND completed!=0 AND NOT EXISTS  (SELECT * FROM clouddrive_objects WHERE trans_id=t.id)", false);
	}
	q_getDeletableTransactions->Bind(curr_trans_id);
	db_results res=q_getDeletableTransactions->Read();
	q_getDeletableTransactions->Reset();
	std::vector<int64> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi64(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int64> KvStoreDao::getDeletableTransactionsCd
* @return int64 id
* @sql
*   SELECT id FROM clouddrive_transactions_cd t WHERE cd_id=:cd_id(int64) AND
*       id<:curr_trans_id(int64) AND completed!=0 AND NOT EXISTS
*          (SELECT * FROM clouddrive_objects WHERE trans_id=t.id)
*/
std::vector<int64> KvStoreDao::getDeletableTransactionsCd(int64 cd_id, int64 curr_trans_id)
{
	if(q_getDeletableTransactionsCd==nullptr)
	{
		q_getDeletableTransactionsCd=db->Prepare("SELECT id FROM clouddrive_transactions_cd t WHERE cd_id=? AND id<? AND completed!=0 AND NOT EXISTS (SELECT * FROM clouddrive_objects WHERE trans_id=t.id)", false);
	}
	q_getDeletableTransactionsCd->Bind(cd_id);
	q_getDeletableTransactionsCd->Bind(curr_trans_id);
	db_results res=q_getDeletableTransactionsCd->Read();
	q_getDeletableTransactionsCd->Reset();
	std::vector<int64> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi64(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int64> KvStoreDao::getLastFinalizedTransactions
* @return int64 id
* @sql
*   SELECT id FROM clouddrive_transactions WHERE 
*			completed=1	AND id>:last_trans_id(int64) AND id<:curr_complete_trans_id(int64)
*/
std::vector<int64> KvStoreDao::getLastFinalizedTransactions(int64 last_trans_id, int64 curr_complete_trans_id)
{
	if(q_getLastFinalizedTransactions==nullptr)
	{
		q_getLastFinalizedTransactions=db->Prepare("SELECT id FROM clouddrive_transactions WHERE  completed=1	AND id>? AND id<?", false);
	}
	q_getLastFinalizedTransactions->Bind(last_trans_id);
	q_getLastFinalizedTransactions->Bind(curr_complete_trans_id);
	db_results res=q_getLastFinalizedTransactions->Read();
	q_getLastFinalizedTransactions->Reset();
	std::vector<int64> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi64(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int64> KvStoreDao::getLastFinalizedTransactionsCd
* @return int64 id
* @sql
*   SELECT id FROM clouddrive_transactions_cd WHERE
*			completed=1	AND cd_id=:cd_id(int64) AND id>:last_trans_id(int64) AND id<:curr_complete_trans_id(int64)
*/
std::vector<int64> KvStoreDao::getLastFinalizedTransactionsCd(int64 cd_id, int64 last_trans_id, int64 curr_complete_trans_id)
{
	if(q_getLastFinalizedTransactionsCd==nullptr)
	{
		q_getLastFinalizedTransactionsCd=db->Prepare("SELECT id FROM clouddrive_transactions_cd WHERE completed=1	AND cd_id=? AND id>? AND id<?", false);
	}
	q_getLastFinalizedTransactionsCd->Bind(cd_id);
	q_getLastFinalizedTransactionsCd->Bind(last_trans_id);
	q_getLastFinalizedTransactionsCd->Bind(curr_complete_trans_id);
	db_results res=q_getLastFinalizedTransactionsCd->Read();
	q_getLastFinalizedTransactionsCd->Reset();
	std::vector<int64> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi64(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdDelObjectMd5> KvStoreDao::getDeletableObjectsMd5Ordered
* @return int64 trans_id, blob tkey, string md5sum
* @sql
*   SELECT trans_id, tkey, md5sum FROM clouddrive_objects
*          WHERE trans_id<:curr_trans_id(int64) AND
*           tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*			 AND size != -1
*			ORDER BY trans_id ASC, tkey ASC
*/
std::vector<KvStoreDao::CdDelObjectMd5> KvStoreDao::getDeletableObjectsMd5Ordered(int64 curr_trans_id)
{
	if(q_getDeletableObjectsMd5Ordered==nullptr)
	{
		q_getDeletableObjectsMd5Ordered=db->Prepare("SELECT trans_id, tkey, md5sum FROM clouddrive_objects WHERE trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?) AND size != -1 ORDER BY trans_id ASC, tkey ASC", false);
	}
	q_getDeletableObjectsMd5Ordered->Bind(curr_trans_id);
	q_getDeletableObjectsMd5Ordered->Bind(curr_trans_id);
	db_results res=q_getDeletableObjectsMd5Ordered->Read();
	q_getDeletableObjectsMd5Ordered->Reset();
	std::vector<KvStoreDao::CdDelObjectMd5> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdDelObjectMd5> KvStoreDao::getDeletableObjectsMd5Cd
* @return int64 trans_id, blob tkey, string md5sum
* @sql
*   SELECT trans_id, tkey, md5sum FROM clouddrive_objects_cd
*          WHERE cd_id=:cd_id(int64) AND trans_id<:curr_trans_id(int64) AND
*           tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*			 AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<KvStoreDao::CdDelObjectMd5> KvStoreDao::getDeletableObjectsMd5Cd(int64 cd_id, int64 curr_trans_id)
{
	if(q_getDeletableObjectsMd5Cd==nullptr)
	{
		q_getDeletableObjectsMd5Cd=db->Prepare("SELECT trans_id, tkey, md5sum FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?) AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getDeletableObjectsMd5Cd->Bind(cd_id);
	q_getDeletableObjectsMd5Cd->Bind(curr_trans_id);
	q_getDeletableObjectsMd5Cd->Bind(curr_trans_id);
	db_results res=q_getDeletableObjectsMd5Cd->Read();
	q_getDeletableObjectsMd5Cd->Reset();
	std::vector<KvStoreDao::CdDelObjectMd5> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdDelObjectMd5> KvStoreDao::getDeletableObjectsMd5
* @return int64 trans_id, blob tkey, string md5sum
* @sql
*   SELECT trans_id, tkey, md5sum FROM clouddrive_objects
*          WHERE trans_id<:curr_trans_id(int64) AND
*           tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*			 AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<KvStoreDao::CdDelObjectMd5> KvStoreDao::getDeletableObjectsMd5(int64 curr_trans_id)
{
	if(q_getDeletableObjectsMd5==nullptr)
	{
		q_getDeletableObjectsMd5=db->Prepare("SELECT trans_id, tkey, md5sum FROM clouddrive_objects WHERE trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?) AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getDeletableObjectsMd5->Bind(curr_trans_id);
	q_getDeletableObjectsMd5->Bind(curr_trans_id);
	db_results res=q_getDeletableObjectsMd5->Read();
	q_getDeletableObjectsMd5->Reset();
	std::vector<KvStoreDao::CdDelObjectMd5> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdDelObject> KvStoreDao::getDeletableObjectsOrdered
* @return int64 trans_id, blob tkey
* @sql
*   SELECT trans_id, tkey FROM clouddrive_objects
*          WHERE trans_id<:curr_trans_id(int64) AND
*           tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*			 AND size != -1
*			ORDER BY trans_id ASC, tkey ASC
*/
std::vector<KvStoreDao::CdDelObject> KvStoreDao::getDeletableObjectsOrdered(int64 curr_trans_id)
{
	if(q_getDeletableObjectsOrdered==nullptr)
	{
		q_getDeletableObjectsOrdered=db->Prepare("SELECT trans_id, tkey FROM clouddrive_objects WHERE trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?) AND size != -1 ORDER BY trans_id ASC, tkey ASC", false);
	}
	q_getDeletableObjectsOrdered->Bind(curr_trans_id);
	q_getDeletableObjectsOrdered->Bind(curr_trans_id);
	db_results res=q_getDeletableObjectsOrdered->Read();
	q_getDeletableObjectsOrdered->Reset();
	std::vector<KvStoreDao::CdDelObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdDelObject> KvStoreDao::getDeletableObjects
* @return int64 trans_id, blob tkey
* @sql
*   SELECT trans_id, tkey FROM clouddrive_objects
*          WHERE trans_id<:curr_trans_id(int64) AND
*           tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*			 AND size != -1
*			ORDER BY tkey ASC
*/
std::vector<KvStoreDao::CdDelObject> KvStoreDao::getDeletableObjects(int64 curr_trans_id)
{
	if(q_getDeletableObjects==nullptr)
	{
		q_getDeletableObjects=db->Prepare("SELECT trans_id, tkey FROM clouddrive_objects WHERE trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?) AND size != -1 ORDER BY tkey ASC", false);
	}
	q_getDeletableObjects->Bind(curr_trans_id);
	q_getDeletableObjects->Bind(curr_trans_id);
	db_results res=q_getDeletableObjects->Read();
	q_getDeletableObjects->Reset();
	std::vector<KvStoreDao::CdDelObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::deleteDeletableObjects
* @sql
*   DELETE FROM clouddrive_objects
*           WHERE trans_id<:curr_trans_id(int64) AND
*            tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*/
bool KvStoreDao::deleteDeletableObjects(int64 curr_trans_id)
{
	if(q_deleteDeletableObjects==nullptr)
	{
		q_deleteDeletableObjects=db->Prepare("DELETE FROM clouddrive_objects WHERE trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?)", false);
	}
	q_deleteDeletableObjects->Bind(curr_trans_id);
	q_deleteDeletableObjects->Bind(curr_trans_id);
	bool ret = q_deleteDeletableObjects->Write();
	q_deleteDeletableObjects->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::deleteDeletableObjectsCd
* @sql
*   DELETE FROM clouddrive_objects_cd
*           WHERE cd_id=:cd_id(int64) AND trans_id<:curr_trans_id(int64) AND
*            tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=:curr_trans_id(int64))
*/
bool KvStoreDao::deleteDeletableObjectsCd(int64 cd_id, int64 curr_trans_id)
{
	if(q_deleteDeletableObjectsCd==nullptr)
	{
		q_deleteDeletableObjectsCd=db->Prepare("DELETE FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id<? AND tkey IN (SELECT tkey FROM clouddrive_objects WHERE trans_id=?)", false);
	}
	q_deleteDeletableObjectsCd->Bind(cd_id);
	q_deleteDeletableObjectsCd->Bind(curr_trans_id);
	q_deleteDeletableObjectsCd->Bind(curr_trans_id);
	bool ret = q_deleteDeletableObjectsCd->Write();
	q_deleteDeletableObjectsCd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::addDelMarkerObject
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey, size)
*        VALUES (:trans_id(int64), :tkey(blob), -1)
*/
void KvStoreDao::addDelMarkerObject(int64 trans_id, const std::string& tkey)
{
	if(q_addDelMarkerObject==nullptr)
	{
		q_addDelMarkerObject=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey, size) VALUES (?, ?, -1)", false);
	}
	q_addDelMarkerObject->Bind(trans_id);
	q_addDelMarkerObject->Bind(tkey.c_str(), (_u32)tkey.size());
	q_addDelMarkerObject->Write();
	q_addDelMarkerObject->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::addDelMarkerObjectCd
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey, size)
*        VALUES (:cd_id(int64), :trans_id(int64), :tkey(blob), -1)
*/
void KvStoreDao::addDelMarkerObjectCd(int64 cd_id, int64 trans_id, const std::string& tkey)
{
	if(q_addDelMarkerObjectCd==nullptr)
	{
		q_addDelMarkerObjectCd=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey, size) VALUES (?, ?, ?, -1)", false);
	}
	q_addDelMarkerObjectCd->Bind(cd_id);
	q_addDelMarkerObjectCd->Bind(trans_id);
	q_addDelMarkerObjectCd->Bind(tkey.c_str(), (_u32)tkey.size());
	q_addDelMarkerObjectCd->Write();
	q_addDelMarkerObjectCd->Reset();
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::addObject
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey, md5sum, size)
*        VALUES (:trans_id(int64), :tkey(blob), :md5sum(blob), :size(int64) )
*/
bool KvStoreDao::addObject(int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size)
{
	if(q_addObject==nullptr)
	{
		q_addObject=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey, md5sum, size) VALUES (?, ?, ?, ? )", false);
	}
	q_addObject->Bind(trans_id);
	q_addObject->Bind(tkey.c_str(), (_u32)tkey.size());
	q_addObject->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_addObject->Bind(size);
	bool ret = q_addObject->Write();
	q_addObject->Reset();
	return ret;
}


/**
* @-SQLGenAccess
* @func bool KvStoreDao::addObjectCd
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey, md5sum, size)
*        VALUES (:cd_id(int64), :trans_id(int64), :tkey(blob), :md5sum(blob), :size(int64) )
*/
bool KvStoreDao::addObjectCd(int64 cd_id, int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size)
{
	if(q_addObjectCd==nullptr)
	{
		q_addObjectCd=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey, md5sum, size) VALUES (?, ?, ?, ?, ? )", false);
	}
	q_addObjectCd->Bind(cd_id);
	q_addObjectCd->Bind(trans_id);
	q_addObjectCd->Bind(tkey.c_str(), (_u32)tkey.size());
	q_addObjectCd->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_addObjectCd->Bind(size);
	bool ret = q_addObjectCd->Write();
	q_addObjectCd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::addObject2
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey, md5sum, size, last_modified)
*        VALUES (:trans_id(int64), :tkey(blob), :md5sum(blob), :size(int64), :last_modified(int64) )
*/
bool KvStoreDao::addObject2(int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size, int64 last_modified)
{
	if(q_addObject2==nullptr)
	{
		q_addObject2=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey, md5sum, size, last_modified) VALUES (?, ?, ?, ?, ? )", false);
	}
	q_addObject2->Bind(trans_id);
	q_addObject2->Bind(tkey.c_str(), (_u32)tkey.size());
	q_addObject2->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_addObject2->Bind(size);
	q_addObject2->Bind(last_modified);
	bool ret = q_addObject2->Write();
	q_addObject2->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::addObject2Cd
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey, md5sum, size, last_modified)
*        VALUES (:cd_id(int64), :trans_id(int64), :tkey(blob), :md5sum(blob), :size(int64), :last_modified(int64) )
*/
bool KvStoreDao::addObject2Cd(int64 cd_id, int64 trans_id, const std::string& tkey, const std::string& md5sum, int64 size, int64 last_modified)
{
	if(q_addObject2Cd==nullptr)
	{
		q_addObject2Cd=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey, md5sum, size, last_modified) VALUES (?, ?, ?, ?, ?, ? )", false);
	}
	q_addObject2Cd->Bind(cd_id);
	q_addObject2Cd->Bind(trans_id);
	q_addObject2Cd->Bind(tkey.c_str(), (_u32)tkey.size());
	q_addObject2Cd->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_addObject2Cd->Bind(size);
	q_addObject2Cd->Bind(last_modified);
	bool ret = q_addObject2Cd->Write();
	q_addObject2Cd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::addPartialObject
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey)
*        VALUES (:trans_id(int64), :tkey(blob) )
*/
bool KvStoreDao::addPartialObject(int64 trans_id, const std::string& tkey)
{
	if(q_addPartialObject==nullptr)
	{
		q_addPartialObject=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects (trans_id, tkey) VALUES (?, ? )", false);
	}
	q_addPartialObject->Bind(trans_id);
	q_addPartialObject->Bind(tkey.c_str(), (_u32)tkey.size());
	bool ret = q_addPartialObject->Write();
	q_addPartialObject->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::addPartialObjectCd
* @sql
*   INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey)
*        VALUES (:cd_id(int64), :trans_id(int64), :tkey(blob) )
*/
bool KvStoreDao::addPartialObjectCd(int64 cd_id, int64 trans_id, const std::string& tkey)
{
	if(q_addPartialObjectCd==nullptr)
	{
		q_addPartialObjectCd=db->Prepare("INSERT OR REPLACE INTO clouddrive_objects_cd (cd_id, trans_id, tkey) VALUES (?, ?, ? )", false);
	}
	q_addPartialObjectCd->Bind(cd_id);
	q_addPartialObjectCd->Bind(trans_id);
	q_addPartialObjectCd->Bind(tkey.c_str(), (_u32)tkey.size());
	bool ret = q_addPartialObjectCd->Write();
	q_addPartialObjectCd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::updateObjectSearch
* @sql
*   UPDATE clouddrive_objects SET md5sum=:md5sum(blob), size=:size(int64)
*		WHERE tkey=:tkey(blob) AND trans_id=:trans_id(int64)
*/
bool KvStoreDao::updateObjectSearch(const std::string& md5sum, int64 size, const std::string& tkey, int64 trans_id)
{
	if(q_updateObjectSearch==nullptr)
	{
		q_updateObjectSearch=db->Prepare("UPDATE clouddrive_objects SET md5sum=?, size=? WHERE tkey=? AND trans_id=?", false);
	}
	q_updateObjectSearch->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObjectSearch->Bind(size);
	q_updateObjectSearch->Bind(tkey.c_str(), (_u32)tkey.size());
	q_updateObjectSearch->Bind(trans_id);
	bool ret = q_updateObjectSearch->Write();
	q_updateObjectSearch->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::updateObject
* @sql
*   UPDATE clouddrive_objects SET md5sum=:md5sum(blob), size=:size(int64)
*		WHERE rowid=:rowid(int64)
*/
bool KvStoreDao::updateObject(const std::string& md5sum, int64 size, int64 rowid)
{
	if(q_updateObject==nullptr)
	{
		q_updateObject=db->Prepare("UPDATE clouddrive_objects SET md5sum=?, size=? WHERE rowid=?", false);
	}
	q_updateObject->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObject->Bind(size);
	q_updateObject->Bind(rowid);
	bool ret = q_updateObject->Write();
	q_updateObject->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::updateObjectCd
* @sql
*   UPDATE clouddrive_objects_cd SET md5sum=:md5sum(blob), size=:size(int64)
*		WHERE rowid=:rowid(int64)
*/
bool KvStoreDao::updateObjectCd(const std::string& md5sum, int64 size, int64 rowid)
{
	if(q_updateObjectCd==nullptr)
	{
		q_updateObjectCd=db->Prepare("UPDATE clouddrive_objects_cd SET md5sum=?, size=? WHERE rowid=?", false);
	}
	q_updateObjectCd->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObjectCd->Bind(size);
	q_updateObjectCd->Bind(rowid);
	bool ret = q_updateObjectCd->Write();
	q_updateObjectCd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::updateObject2Cd
* @sql
*   UPDATE clouddrive_objects_cd SET md5sum=:md5sum(blob), size=:size(int64), last_modified=:last_modified(int64)
*		WHERE rowid=:rowid(int64)
*/
bool KvStoreDao::updateObject2Cd(const std::string& md5sum, int64 size, int64 last_modified, int64 rowid)
{
	if(q_updateObject2Cd==nullptr)
	{
		q_updateObject2Cd=db->Prepare("UPDATE clouddrive_objects_cd SET md5sum=?, size=?, last_modified=? WHERE rowid=?", false);
	}
	q_updateObject2Cd->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObject2Cd->Bind(size);
	q_updateObject2Cd->Bind(last_modified);
	q_updateObject2Cd->Bind(rowid);
	bool ret = q_updateObject2Cd->Write();
	q_updateObject2Cd->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::updateObject2
* @sql
*   UPDATE clouddrive_objects SET md5sum=:md5sum(blob), size=:size(int64), last_modified=:last_modified(int64)
*		WHERE rowid=:rowid(int64)
*/
bool KvStoreDao::updateObject2(const std::string& md5sum, int64 size, int64 last_modified, int64 rowid)
{
	if(q_updateObject2==nullptr)
	{
		q_updateObject2=db->Prepare("UPDATE clouddrive_objects SET md5sum=?, size=?, last_modified=? WHERE rowid=?", false);
	}
	q_updateObject2->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObject2->Bind(size);
	q_updateObject2->Bind(last_modified);
	q_updateObject2->Bind(rowid);
	bool ret = q_updateObject2->Write();
	q_updateObject2->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::deletePartialObject
* @sql
*		DELETE FROM clouddrive_objects WHERE rowid=:rowid(int64)
*/
void KvStoreDao::deletePartialObject(int64 rowid)
{
	if(q_deletePartialObject==nullptr)
	{
		q_deletePartialObject=db->Prepare("DELETE FROM clouddrive_objects WHERE rowid=?", false);
	}
	q_deletePartialObject->Bind(rowid);
	q_deletePartialObject->Write();
	q_deletePartialObject->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::deletePartialObjectCd
* @sql
*		DELETE FROM clouddrive_objects_cd WHERE rowid=:rowid(int64)
*/
void KvStoreDao::deletePartialObjectCd(int64 rowid)
{
	if(q_deletePartialObjectCd==nullptr)
	{
		q_deletePartialObjectCd=db->Prepare("DELETE FROM clouddrive_objects_cd WHERE rowid=?", false);
	}
	q_deletePartialObjectCd->Bind(rowid);
	q_deletePartialObjectCd->Write();
	q_deletePartialObjectCd->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::updateGeneration
* @sql
*   UPDATE clouddrive_generation SET generation=:generation(int64)
*/
void KvStoreDao::updateGeneration(int64 generation)
{
	if(q_updateGeneration==nullptr)
	{
		q_updateGeneration=db->Prepare("UPDATE clouddrive_generation SET generation=?", false);
	}
	q_updateGeneration->Bind(generation);
	q_updateGeneration->Write();
	q_updateGeneration->Reset();
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getGeneration
* @return int64 generation
* @sql
*   SELECT generation FROM clouddrive_generation
*/
KvStoreDao::CondInt64 KvStoreDao::getGeneration(void)
{
	if(q_getGeneration==nullptr)
	{
		q_getGeneration=db->Prepare("SELECT generation FROM clouddrive_generation", false);
	}
	db_results res=q_getGeneration->Read();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["generation"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getGenerationCd
* @return int64 generation
* @sql
*   SELECT generation FROM clouddrive_generation_cd WHERE cd_id=:cd_id(int64)
*/
KvStoreDao::CondInt64 KvStoreDao::getGenerationCd(int64 cd_id)
{
	if(q_getGenerationCd==nullptr)
	{
		q_getGenerationCd=db->Prepare("SELECT generation FROM clouddrive_generation_cd WHERE cd_id=?", false);
	}
	q_getGenerationCd->Bind(cd_id);
	db_results res=q_getGenerationCd->Read();
	q_getGenerationCd->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["generation"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::insertGeneration
* @sql
*   INSERT INTO clouddrive_generation (generation) VALUES (:generation(int64))
*/
void KvStoreDao::insertGeneration(int64 generation)
{
	if(q_insertGeneration==nullptr)
	{
		q_insertGeneration=db->Prepare("INSERT INTO clouddrive_generation (generation) VALUES (?)", false);
	}
	q_insertGeneration->Bind(generation);
	q_insertGeneration->Write();
	q_insertGeneration->Reset();
}

/**
* @-SQLGenAccess
* @func CdObject KvStoreDao::getObjectInTransid
* @return int64 trans_id, int64 size, blob md5sum
* @sql
*		SELECT trans_id, size, md5sum FROM
*				clouddrive_objects WHERE trans_id=:trans_id(int64) 
*					AND tkey=:tkey(blob) AND size!=-1
*
*/
KvStoreDao::CdObject KvStoreDao::getObjectInTransid(int64 trans_id, const std::string& tkey)
{
	if(q_getObjectInTransid==nullptr)
	{
		q_getObjectInTransid=db->Prepare("SELECT trans_id, size, md5sum FROM clouddrive_objects WHERE trans_id=?  AND tkey=? AND size!=-1", false);
	}
	q_getObjectInTransid->Bind(trans_id);
	q_getObjectInTransid->Bind(tkey.c_str(), (_u32)tkey.size());
	db_results res=q_getObjectInTransid->Read();
	q_getObjectInTransid->Reset();
	CdObject ret = { false, 0, 0, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.size=watoi64(res[0]["size"]);
		ret.md5sum=res[0]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func CdObject KvStoreDao::getObjectInTransidCd
* @return int64 trans_id, int64 size, blob md5sum
* @sql
*		SELECT trans_id, size, md5sum FROM
*				clouddrive_objects_cd WHERE cd_id=:cd_id(int64) AND trans_id=:trans_id(int64)
*					AND tkey=:tkey(blob) AND size!=-1
*
*/
KvStoreDao::CdObject KvStoreDao::getObjectInTransidCd(int64 cd_id, int64 trans_id, const std::string& tkey)
{
	if(q_getObjectInTransidCd==nullptr)
	{
		q_getObjectInTransidCd=db->Prepare("SELECT trans_id, size, md5sum FROM clouddrive_objects_cd WHERE cd_id=? AND trans_id=? AND tkey=? AND size!=-1", false);
	}
	q_getObjectInTransidCd->Bind(cd_id);
	q_getObjectInTransidCd->Bind(trans_id);
	q_getObjectInTransidCd->Bind(tkey.c_str(), (_u32)tkey.size());
	db_results res=q_getObjectInTransidCd->Read();
	q_getObjectInTransidCd->Reset();
	CdObject ret = { false, 0, 0, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.size=watoi64(res[0]["size"]);
		ret.md5sum=res[0]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func CdSingleObject KvStoreDao::getSingleObject
* @return string tkey, int64 trans_id, int64 size, blob md5sum
* @sql
*		SELECT tkey, trans_id, size, md5sum FROM
*				clouddrive_objects WHERE size!=-1 LIMIT 1
*
*/
KvStoreDao::CdSingleObject KvStoreDao::getSingleObject(void)
{
	if(q_getSingleObject==nullptr)
	{
		q_getSingleObject=db->Prepare("SELECT tkey, trans_id, size, md5sum FROM clouddrive_objects WHERE size!=-1 LIMIT 1", false);
	}
	db_results res=q_getSingleObject->Read();
	CdSingleObject ret = { false, "", 0, 0, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.tkey=res[0]["tkey"];
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.size=watoi64(res[0]["size"]);
		ret.md5sum=res[0]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func CdObject KvStoreDao::getObject
* @return int64 trans_id, int64 size, blob md5sum
* @sql
*		SELECT trans_id, size, md5sum, 0 AS cd_id FROM
*				(clouddrive_objects INNER JOIN clouddrive_transactions
*					ON trans_id=clouddrive_transactions.id) WHERE
*					    trans_id<=:curr_trans_id(int64) AND
*						tkey=:tkey(blob) AND active=1
*						ORDER BY trans_id DESC LIMIT 1
*
*/
KvStoreDao::CdObject KvStoreDao::getObject(int64 curr_trans_id, const std::string& tkey)
{
	if(q_getObject==nullptr)
	{
		q_getObject=db->Prepare("SELECT trans_id, size, md5sum, 0 AS cd_id FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE trans_id<=? AND tkey=? AND active=1 ORDER BY trans_id DESC LIMIT 1", false);
	}
	q_getObject->Bind(curr_trans_id);
	q_getObject->Bind(tkey.c_str(), (_u32)tkey.size());
	db_results res=q_getObject->Read();
	q_getObject->Reset();
	CdObject ret = { false, 0, 0, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.size=watoi64(res[0]["size"]);
		ret.md5sum=res[0]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func CdObject KvStoreDao::getObjectCd
* @return int64 trans_id, int64 size, blob md5sum
* @sql
*		SELECT trans_id, size, md5sum FROM
*				(clouddrive_objects_cd INNER JOIN clouddrive_transactions_cd
*					ON trans_id=clouddrive_transactions_cd.id) WHERE
						clouddrive_objects_cd.cd_id=:cd_id(int64) AND
*					    trans_id<=:curr_trans_id(int64) AND
*						tkey=:tkey(blob) AND active=1						
*						ORDER BY trans_id DESC LIMIT 1
*
*/
KvStoreDao::CdObject KvStoreDao::getObjectCd(int64 cd_id, int64 curr_trans_id, const std::string& tkey)
{
	if(q_getObjectCd==nullptr)
	{
		q_getObjectCd=db->Prepare("SELECT trans_id, size, md5sum FROM (clouddrive_objects_cd INNER JOIN clouddrive_transactions_cd ON trans_id=clouddrive_transactions_cd.id) WHERE clouddrive_objects_cd.cd_id=? AND trans_id<=? AND tkey=? AND active=1						 ORDER BY trans_id DESC LIMIT 1", false);
	}
	q_getObjectCd->Bind(cd_id);
	q_getObjectCd->Bind(curr_trans_id);
	q_getObjectCd->Bind(tkey.c_str(), (_u32)tkey.size());
	db_results res=q_getObjectCd->Read();
	q_getObjectCd->Reset();
	CdObject ret = { false, 0, 0, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.trans_id=watoi64(res[0]["trans_id"]);
		ret.size=watoi64(res[0]["size"]);
		ret.md5sum=res[0]["md5sum"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::isTransactionActive
* @return int64 id
* @sql
*		SELECT id FROM clouddrive_transactions WHERE active=1 AND id=:trans_id(int64)
*
*/
KvStoreDao::CondInt64 KvStoreDao::isTransactionActive(int64 trans_id)
{
	if(q_isTransactionActive==nullptr)
	{
		q_isTransactionActive=db->Prepare("SELECT id FROM clouddrive_transactions WHERE active=1 AND id=?", false);
	}
	q_isTransactionActive->Bind(trans_id);
	db_results res=q_isTransactionActive->Read();
	q_isTransactionActive->Reset();
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
* @func int64 KvStoreDao::isTransactionActiveCd
* @return int64 id
* @sql
*		SELECT id FROM clouddrive_transactions_cd WHERE active=1 AND cd_id=:cd_id(int64) AND id=:trans_id(int64)
*
*/
KvStoreDao::CondInt64 KvStoreDao::isTransactionActiveCd(int64 cd_id, int64 trans_id)
{
	if(q_isTransactionActiveCd==nullptr)
	{
		q_isTransactionActiveCd=db->Prepare("SELECT id FROM clouddrive_transactions_cd WHERE active=1 AND cd_id=? AND id=?", false);
	}
	q_isTransactionActiveCd->Bind(cd_id);
	q_isTransactionActiveCd->Bind(trans_id);
	db_results res=q_isTransactionActiveCd->Read();
	q_isTransactionActiveCd->Reset();
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
* @func bool KvStoreDao::deleteObject
* @sql
*		DELETE FROM clouddrive_objects WHERE trans_id=:trans_id(int64)
*					AND tkey=:tkey(blob)
*
*/
bool KvStoreDao::deleteObject(int64 trans_id, const std::string& tkey)
{
	if(q_deleteObject==nullptr)
	{
		q_deleteObject=db->Prepare("DELETE FROM clouddrive_objects WHERE trans_id=? AND tkey=?", false);
	}
	q_deleteObject->Bind(trans_id);
	q_deleteObject->Bind(tkey.c_str(), (_u32)tkey.size());
	bool ret = q_deleteObject->Write();
	q_deleteObject->Reset();
	return ret;
}

/**
* @-SQLGenAccess
* @func string KvStoreDao::getMiscValue
* @return string value
* @sql
*		SELECT value FROM misc WHERE key=:key(string)
*
*/
KvStoreDao::CondString KvStoreDao::getMiscValue(const std::string& key)
{
	if(q_getMiscValue==nullptr)
	{
		q_getMiscValue=db->Prepare("SELECT value FROM misc WHERE key=?", false);
	}
	q_getMiscValue->Bind(key);
	db_results res=q_getMiscValue->Read();
	q_getMiscValue->Reset();
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
* @func void KvStoreDao::setMiscValue
* @sql
*		INSERT OR REPLACE INTO misc (key, value) VALUES (:key(string), :value(string))
*/
void KvStoreDao::setMiscValue(const std::string& key, const std::string& value)
{
	if(q_setMiscValue==nullptr)
	{
		q_setMiscValue=db->Prepare("INSERT OR REPLACE INTO misc (key, value) VALUES (?, ?)", false);
	}
	q_setMiscValue->Bind(key);
	q_setMiscValue->Bind(value);
	q_setMiscValue->Write();
	q_setMiscValue->Reset();
}

/**
* @-SQLGenAccess
* @func STransactionProperties KvStoreDao::getTransactionProperties
* @return int active, int completed, int64 cd_id
* @sql
*		SELECT active, completed, 0 AS cd_id FROM clouddrive_transactions WHERE id=:id(int64)
*/
KvStoreDao::STransactionProperties KvStoreDao::getTransactionProperties(int64 id)
{
	if(q_getTransactionProperties==nullptr)
	{
		q_getTransactionProperties=db->Prepare("SELECT active, completed, 0 AS cd_id FROM clouddrive_transactions WHERE id=?", false);
	}
	q_getTransactionProperties->Bind(id);
	db_results res=q_getTransactionProperties->Read();
	q_getTransactionProperties->Reset();
	STransactionProperties ret = { false, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.active=watoi(res[0]["active"]);
		ret.completed=watoi(res[0]["completed"]);
		ret.cd_id=watoi64(res[0]["cd_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func STransactionProperties KvStoreDao::getTransactionPropertiesCd
* @return int active, int completed, int64 cd_id
* @sql
*		SELECT active, completed, cd_id FROM clouddrive_transactions_cd WHERE cd_id=:cd_id(int64) AND id=:id(int64)
*/
KvStoreDao::STransactionProperties KvStoreDao::getTransactionPropertiesCd(int64 cd_id, int64 id)
{
	if(q_getTransactionPropertiesCd==nullptr)
	{
		q_getTransactionPropertiesCd=db->Prepare("SELECT active, completed, cd_id FROM clouddrive_transactions_cd WHERE cd_id=? AND id=?", false);
	}
	q_getTransactionPropertiesCd->Bind(cd_id);
	q_getTransactionPropertiesCd->Bind(id);
	db_results res=q_getTransactionPropertiesCd->Read();
	q_getTransactionPropertiesCd->Reset();
	STransactionProperties ret = { false, 0, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.active=watoi(res[0]["active"]);
		ret.completed=watoi(res[0]["completed"]);
		ret.cd_id=watoi64(res[0]["cd_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdIterObject> KvStoreDao::getInitialObjectsLM
* @return int64 trans_id, blob tkey, blob md5sum, int64 size, int64 last_modified
* @sql
*		SELECT trans_id, tkey, md5sum, size, last_modified 
*		FROM clouddrive_objects WHERE size!=-1 ORDER BY last_modified ASC LIMIT 10000
*/
std::vector<KvStoreDao::CdIterObject> KvStoreDao::getInitialObjectsLM(void)
{
	if(q_getInitialObjectsLM==nullptr)
	{
		q_getInitialObjectsLM=db->Prepare("SELECT trans_id, tkey, md5sum, size, last_modified  FROM clouddrive_objects WHERE size!=-1 ORDER BY last_modified ASC LIMIT 10000", false);
	}
	db_results res=q_getInitialObjectsLM->Read();
	std::vector<KvStoreDao::CdIterObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
		ret[i].size=watoi64(res[i]["size"]);
		ret[i].last_modified=watoi64(res[i]["last_modified"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdIterObject> KvStoreDao::getInitialObjects
* @return int64 trans_id, blob tkey, blob md5sum, int64 size
* @sql
*		SELECT trans_id, tkey, md5sum, size FROM clouddrive_objects WHERE size!=-1 ORDER BY tkey ASC, trans_id ASC LIMIT 10000
*/
std::vector<KvStoreDao::CdIterObject> KvStoreDao::getInitialObjects(void)
{
	if(q_getInitialObjects==nullptr)
	{
		q_getInitialObjects=db->Prepare("SELECT trans_id, tkey, md5sum, size FROM clouddrive_objects WHERE size!=-1 ORDER BY tkey ASC, trans_id ASC LIMIT 10000", false);
	}
	db_results res=q_getInitialObjects->Read();
	std::vector<KvStoreDao::CdIterObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
		ret[i].size=watoi64(res[i]["size"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdIterObject> KvStoreDao::getIterObjectsLMInit
* @return int64 trans_id, blob tkey, blob md5sum, int64 size, int64 last_modified
* @sql
*		SELECT trans_id, tkey, md5sum, size, last_modified FROM
*			(clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id)
*		WHERE last_modified>=:last_modified_start(int64) AND size!=-1 AND active!=0
*		ORDER BY last_modified ASC LIMIT 10000
*/
std::vector<KvStoreDao::CdIterObject> KvStoreDao::getIterObjectsLMInit(int64 last_modified_start)
{
	if(q_getIterObjectsLMInit==nullptr)
	{
		q_getIterObjectsLMInit=db->Prepare("SELECT trans_id, tkey, md5sum, size, last_modified FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE last_modified>=? AND size!=-1 AND active!=0 ORDER BY last_modified ASC LIMIT 10000", false);
	}
	q_getIterObjectsLMInit->Bind(last_modified_start);
	db_results res=q_getIterObjectsLMInit->Read();
	q_getIterObjectsLMInit->Reset();
	std::vector<KvStoreDao::CdIterObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
		ret[i].size=watoi64(res[i]["size"]);
		ret[i].last_modified=watoi64(res[i]["last_modified"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdIterObject> KvStoreDao::getIterObjectsLM
* @return int64 trans_id, blob tkey, blob md5sum, int64 size, int64 last_modified
* @sql
*		SELECT trans_id, tkey, md5sum, size, last_modified FROM
*			(clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id)
*		WHERE last_modified>:last_modified_start(int64) AND size!=-1 AND active!=0
*			AND last_modified<:last_modified_stop(int64) ORDER BY last_modified ASC LIMIT 10000
*/
std::vector<KvStoreDao::CdIterObject> KvStoreDao::getIterObjectsLM(int64 last_modified_start, int64 last_modified_stop)
{
	if(q_getIterObjectsLM==nullptr)
	{
		q_getIterObjectsLM=db->Prepare("SELECT trans_id, tkey, md5sum, size, last_modified FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE last_modified>? AND size!=-1 AND active!=0 AND last_modified<? ORDER BY last_modified ASC LIMIT 10000", false);
	}
	q_getIterObjectsLM->Bind(last_modified_start);
	q_getIterObjectsLM->Bind(last_modified_stop);
	db_results res=q_getIterObjectsLM->Read();
	q_getIterObjectsLM->Reset();
	std::vector<KvStoreDao::CdIterObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
		ret[i].size=watoi64(res[i]["size"]);
		ret[i].last_modified=watoi64(res[i]["last_modified"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<CdIterObject> KvStoreDao::getIterObjects
* @return int64 trans_id, blob tkey, blob md5sum, int64 size
* @sql
*		SELECT trans_id, tkey, md5sum, size FROM
*			(clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id)
*		WHERE (tkey>:tkey(blob) OR (tkey=:tkey(blob) AND trans_id>:tans_id(int64))) AND size!=-1 AND active!=0
*			ORDER BY tkey ASC, trans_id ASC LIMIT 10000
*/
std::vector<KvStoreDao::CdIterObject> KvStoreDao::getIterObjects(const std::string& tkey, int64 tans_id)
{
	if(q_getIterObjects==nullptr)
	{
		q_getIterObjects=db->Prepare("SELECT trans_id, tkey, md5sum, size FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE (tkey>? OR (tkey=? AND trans_id>?)) AND size!=-1 AND active!=0 ORDER BY tkey ASC, trans_id ASC LIMIT 10000", false);
	}
	q_getIterObjects->Bind(tkey.c_str(), (_u32)tkey.size());
	q_getIterObjects->Bind(tkey.c_str(), (_u32)tkey.size());
	q_getIterObjects->Bind(tans_id);
	db_results res=q_getIterObjects->Read();
	q_getIterObjects->Reset();
	std::vector<KvStoreDao::CdIterObject> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
		ret[i].size=watoi64(res[i]["size"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::updateObjectMd5sum
* @sql
*		UPDATE clouddrive_objects SET md5sum=:md5sum(blob) WHERE trans_id=:transid(int64) AND tkey=:tkey(blob)
*/
void KvStoreDao::updateObjectMd5sum(const std::string& md5sum, int64 transid, const std::string& tkey)
{
	if(q_updateObjectMd5sum==nullptr)
	{
		q_updateObjectMd5sum=db->Prepare("UPDATE clouddrive_objects SET md5sum=? WHERE trans_id=? AND tkey=?", false);
	}
	q_updateObjectMd5sum->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObjectMd5sum->Bind(transid);
	q_updateObjectMd5sum->Bind(tkey.c_str(), (_u32)tkey.size());
	q_updateObjectMd5sum->Write();
	q_updateObjectMd5sum->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::updateObjectMd5sumCd
* @sql
*		UPDATE clouddrive_objects_cd SET md5sum=:md5sum(blob) WHERE cd_id=:cd_id(int64) AND trans_id=:transid(int64) AND tkey=:tkey(blob)
*/
void KvStoreDao::updateObjectMd5sumCd(const std::string& md5sum, int64 cd_id, int64 transid, const std::string& tkey)
{
	if(q_updateObjectMd5sumCd==nullptr)
	{
		q_updateObjectMd5sumCd=db->Prepare("UPDATE clouddrive_objects_cd SET md5sum=? WHERE cd_id=? AND trans_id=? AND tkey=?", false);
	}
	q_updateObjectMd5sumCd->Bind(md5sum.c_str(), (_u32)md5sum.size());
	q_updateObjectMd5sumCd->Bind(cd_id);
	q_updateObjectMd5sumCd->Bind(transid);
	q_updateObjectMd5sumCd->Bind(tkey.c_str(), (_u32)tkey.size());
	q_updateObjectMd5sumCd->Write();
	q_updateObjectMd5sumCd->Reset();
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::insertAllDeletionTasks
* @sql
*		INSERT INTO tasks(task_id, trans_id, created) 
*			SELECT 1 AS task_id, id AS trans_id, 0 AS created FROM clouddrive_transactions a WHERE (completed = 2 OR 
*				(completed = 1 AND EXISTS (
*					SELECT * FROM clouddrive_transactions b WHERE b.id>a.id AND b.completed = 2
*			)) ) AND NOT EXISTS (SELECT * FROM tasks t WHERE t.task_id=1 AND t.trans_id=a.id) ORDER BY id ASC
*/
void KvStoreDao::insertAllDeletionTasks(void)
{
	if(q_insertAllDeletionTasks==nullptr)
	{
		q_insertAllDeletionTasks=db->Prepare("INSERT INTO tasks(task_id, trans_id, created)  SELECT 1 AS task_id, id AS trans_id, 0 AS created FROM clouddrive_transactions a WHERE (completed = 2 OR  (completed = 1 AND EXISTS ( SELECT * FROM clouddrive_transactions b WHERE b.id>a.id AND b.completed = 2 )) ) AND NOT EXISTS (SELECT * FROM tasks t WHERE t.task_id=1 AND t.trans_id=a.id) ORDER BY id ASC", false);
	}
	q_insertAllDeletionTasks->Write();
}

/**
* @-SQLGenAccess
* @func vector<CdIterObject2> KvStoreDao::getUnmirroredObjects
* @return int64 id, int64 trans_id, blob tkey, blob md5sum, int64 size
* @sql
*		SELECT clouddrive_objects.rowid AS id, trans_id, tkey, md5sum, size FROM
*			(clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id)
*		WHERE size!=-1 AND active!=0 AND clouddrive_objects.mirrored=0 AND clouddrive_transactions.completed!=0 AND clouddrive_transactions.active!=0
*			ORDER BY last_modified ASC LIMIT 1000
*/
std::vector<KvStoreDao::CdIterObject2> KvStoreDao::getUnmirroredObjects(void)
{
	if(q_getUnmirroredObjects==nullptr)
	{
		q_getUnmirroredObjects=db->Prepare("SELECT clouddrive_objects.rowid AS id, trans_id, tkey, md5sum, size FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!=-1 AND active!=0 AND clouddrive_objects.mirrored=0 AND clouddrive_transactions.completed!=0 AND clouddrive_transactions.active!=0 ORDER BY last_modified ASC LIMIT 1000", false);
	}
	db_results res=q_getUnmirroredObjects->Read();
	std::vector<KvStoreDao::CdIterObject2> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i]["id"]);
		ret[i].trans_id=watoi64(res[i]["trans_id"]);
		ret[i].tkey=res[i]["tkey"];
		ret[i].md5sum=res[i]["md5sum"];
		ret[i].size=watoi64(res[i]["size"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SUnmirrored KvStoreDao::getUnmirroredObjectsSize
* @return int64 tsize, int64 tcount 
* @sql
*		SELECT SUM(size) AS tsize, COUNT(size) AS tcount FROM
*			(clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id)
*		WHERE size!=-1 AND active!=0 AND clouddrive_objects.mirrored=0 AND clouddrive_transactions.completed!=0 AND clouddrive_transactions.active!=0
*/
KvStoreDao::SUnmirrored KvStoreDao::getUnmirroredObjectsSize(void)
{
	if(q_getUnmirroredObjectsSize==nullptr)
	{
		q_getUnmirroredObjectsSize=db->Prepare("SELECT SUM(size) AS tsize, COUNT(size) AS tcount FROM (clouddrive_objects INNER JOIN clouddrive_transactions ON trans_id=clouddrive_transactions.id) WHERE size!=-1 AND active!=0 AND clouddrive_objects.mirrored=0 AND clouddrive_transactions.completed!=0 AND clouddrive_transactions.active!=0", false);
	}
	db_results res=q_getUnmirroredObjectsSize->Read();
	SUnmirrored ret = { false, 0, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.tsize=watoi64(res[0]["tsize"]);
		ret.tcount=watoi64(res[0]["tcount"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::setObjectMirrored
* @sql
*		UPDATE clouddrive_objects SET mirrored=1 WHERE rowid=:rowid(int64)
*/
void KvStoreDao::setObjectMirrored(int64 rowid)
{
	if(q_setObjectMirrored==nullptr)
	{
		q_setObjectMirrored=db->Prepare("UPDATE clouddrive_objects SET mirrored=1 WHERE rowid=?", false);
	}
	q_setObjectMirrored->Bind(rowid);
	q_setObjectMirrored->Write();
	q_setObjectMirrored->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SCdTrans> KvStoreDao::getUnmirroredTransactions
* @return int64 id, int completed, int active
* @sql
*		SELECT id, completed, active FROM clouddrive_transactions
*		WHERE completed!=0 AND active!=0 AND mirrored=0 AND NOT EXISTS (SELECT * FROM clouddrive_objects WHERE clouddrive_objects.trans_id=clouddrive_transactions.id AND clouddrive_objects.mirrored=0)
*/
std::vector<KvStoreDao::SCdTrans> KvStoreDao::getUnmirroredTransactions(void)
{
	if(q_getUnmirroredTransactions==nullptr)
	{
		q_getUnmirroredTransactions=db->Prepare("SELECT id, completed, active FROM clouddrive_transactions WHERE completed!=0 AND active!=0 AND mirrored=0 AND NOT EXISTS (SELECT * FROM clouddrive_objects WHERE clouddrive_objects.trans_id=clouddrive_transactions.id AND clouddrive_objects.mirrored=0)", false);
	}
	db_results res=q_getUnmirroredTransactions->Read();
	std::vector<KvStoreDao::SCdTrans> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi64(res[i]["id"]);
		ret[i].completed=watoi(res[i]["completed"]);
		ret[i].active=watoi(res[i]["active"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void KvStoreDao::setTransactionMirrored
* @sql
*		UPDATE clouddrive_transactions SET mirrored=1 WHERE id=:id(int64)
*/
void KvStoreDao::setTransactionMirrored(int64 id)
{
	if(q_setTransactionMirrored==nullptr)
	{
		q_setTransactionMirrored=db->Prepare("UPDATE clouddrive_transactions SET mirrored=1 WHERE id=?", false);
	}
	q_setTransactionMirrored->Bind(id);
	q_setTransactionMirrored->Write();
	q_setTransactionMirrored->Reset();
}

/**
* @-SQLGenAccess
* @func bool KvStoreDao::updateGenerationCd
* @sql
*		INSERT OR REPLACE INTO clouddrive_generation_cd (cd_id, generation)
*		VALUES (:cd_id(int64), :generation(int64) )
*/
bool KvStoreDao::updateGenerationCd(int64 cd_id, int64 generation)
{
	if(q_updateGenerationCd==nullptr)
	{
		q_updateGenerationCd=db->Prepare("INSERT OR REPLACE INTO clouddrive_generation_cd (cd_id, generation) VALUES (?, ? )", false);
	}
	q_updateGenerationCd->Bind(cd_id);
	q_updateGenerationCd->Bind(generation);
	bool ret = q_updateGenerationCd->Write();
	q_updateGenerationCd->Reset();
	return ret;
}

IQuery* KvStoreDao::getUpdateGenerationQuery()
{
	return q_updateGeneration;
}

void KvStoreDao::setUpdateGenerationQuery(IQuery* q)
{
	q_updateGeneration = q;
}


/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getLowerTransidObject
* @return int64 trans_id
* @sql
*		SELECT trans_id FROM clouddrive_objects
*			WHERE tkey=:tkey(blob) AND trans_id<:transid(int64) LIMIT 1
*/
KvStoreDao::CondInt64 KvStoreDao::getLowerTransidObject(const std::string& tkey, int64 transid)
{
	if(q_getLowerTransidObject==nullptr)
	{
		q_getLowerTransidObject=db->Prepare("SELECT trans_id FROM clouddrive_objects WHERE tkey=? AND trans_id<? LIMIT 1", false);
	}
	q_getLowerTransidObject->Bind(tkey.c_str(), (_u32)tkey.size());
	q_getLowerTransidObject->Bind(transid);
	db_results res=q_getLowerTransidObject->Read();
	q_getLowerTransidObject->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["trans_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 KvStoreDao::getLowerTransidObjectCd
* @return int64 trans_id
* @sql
*		SELECT trans_id FROM clouddrive_objects_cd
*			WHERE cd_id=:cd_id(int64) AND tkey=:tkey(blob) AND trans_id<:transid(int64) LIMIT 1
*/
KvStoreDao::CondInt64 KvStoreDao::getLowerTransidObjectCd(int64 cd_id, const std::string& tkey, int64 transid)
{
	if(q_getLowerTransidObjectCd==nullptr)
	{
		q_getLowerTransidObjectCd=db->Prepare("SELECT trans_id FROM clouddrive_objects_cd WHERE cd_id=? AND tkey=? AND trans_id<? LIMIT 1", false);
	}
	q_getLowerTransidObjectCd->Bind(cd_id);
	q_getLowerTransidObjectCd->Bind(tkey.c_str(), (_u32)tkey.size());
	q_getLowerTransidObjectCd->Bind(transid);
	db_results res=q_getLowerTransidObjectCd->Read();
	q_getLowerTransidObjectCd->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0]["trans_id"]);
	}
	return ret;
}

