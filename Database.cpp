/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
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

#include "vld.h"
#include "Server.h"
#include "Query.h"
#include "sqlite/sqlite3.h"

IMutex * CDatabase::lock_mutex=NULL;


/*
#ifdef _WIN32
#ifdef _DEBUG
#pragma comment ( lib , "sqlite/sqlite3_dll_debug.lib" )
#else if _RELEASE
#pragma comment ( lib , "sqlite/sqlite3_dll_release.lib" )
#endif
#endif*/

struct UnlockNotification {
  bool fired;                           
  ICondition* cond;                 
  IMutex *mutex;              
};

static int callback(void *CPtr, int argc, char **argv, char **azColName)
{
	CDatabase* db=(CDatabase*)CPtr;
	db_nsingle_result result;
	
	for(int i=0; i<argc; i++)
	{
		//printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
		if( azColName[i] && argv[i])
			result.insert(std::pair<std::string,std::string>(azColName[i], argv[i]) ); 
	}

	db->InsertResults(result);
  
	return 0;
}

static void unlock_notify_cb(void **apArg, int nArg)
{
	for(int i=0; i<nArg; i++)
	{
		UnlockNotification *p = (UnlockNotification *)apArg[i];
		IScopedLock lock(p->mutex);
		p->fired = true;
		p->cond->notify_all();
	}
}

CDatabase::~CDatabase()
{
	destroyAllQueries();
	for(std::map<int, IQuery*>::iterator iter=prepared_queries.begin();iter!=prepared_queries.end();++iter)
	{
		CQuery *q=(CQuery*)iter->second;
		delete q;
	}
	prepared_queries.clear();

	sqlite3_close(db);
}

bool CDatabase::Open(std::string pFile)
{
	if( sqlite3_open(pFile.c_str(), &db) )
	{
		Server->Log("Could not open db ["+pFile+"]");
		sqlite3_close(db);
		return false;
	}
	else
	{
		sqlite3_busy_timeout(db, 50);
		Write("PRAGMA foreign_keys = ON");
		return true;
	}
}

void CDatabase::initMutex(void)
{
	lock_mutex=Server->createMutex();
}

void CDatabase::destroyMutex(void)
{
	Server->destroy(lock_mutex);
}

db_nresults CDatabase::ReadN(std::string pQuery)
{
	//Server->Log("SQL Query(Read): "+pQuery);
	results.clear();
	char *zErrMsg = 0;
	int rc=sqlite3_exec(db, pQuery.c_str(), callback, this, &zErrMsg);
	if( rc!=SQLITE_OK )
	{
		Server->Log("SQL ERROR: "+(std::string)zErrMsg);
	}
	if( zErrMsg!=NULL )
		sqlite3_free(zErrMsg);

	return results;
}

db_results CDatabase::Read(std::string pQuery)
{
	//Server->Log("SQL Query(Read): "+pQuery, LL_DEBUG);
	IQuery *q=Prepare(pQuery, false);
	db_results ret=q->Read();
	delete ((CQuery*)q);
	return ret;
}

bool CDatabase::Write(std::string pQuery)
{
	//Server->Log("SQL Query(Write): "+pQuery, LL_DEBUG);
	IQuery *q=Prepare(pQuery, false);
	if(q!=NULL)
	{
		bool b=q->Write();
		delete ((CQuery*)q);
		return b;
	}
	else
	{
		return false;
	}
}

void CDatabase::InsertResults(const db_nsingle_result &pResult)
{
	results.push_back(pResult);
}


//ToDo: Cache Writings

void CDatabase::BeginTransaction(void)
{
	Write("BEGIN IMMEDIATE;");
}

bool CDatabase::EndTransaction(void)
{
	Write("END;");
	if(lock_mutex->TryLock())
	{
		lock_mutex->Unlock();
		Server->wait(100);
	}
	return true;
}

IQuery* CDatabase::Prepare(std::string pQuery, bool autodestroy)
{
	sqlite3_stmt *prepared_statement;
	const char* tail;
	int err;
	bool transaction_lock=false;
	while((err=sqlite3_prepare_v2(db, pQuery.c_str(), (int)pQuery.size(), &prepared_statement, &tail) )==SQLITE_LOCKED || err==SQLITE_BUSY)
	{
		if(err==SQLITE_LOCKED)
		{
			if(LockForTransaction())
			{
				transaction_lock=true;
				if(!WaitForUnlock())
					Server->Log("DATABASE DEADLOCKED in CDatabase::Prepare", LL_ERROR);
			}
		}		
		else
		{
			if(transaction_lock==false)
			{
				if(LockForTransaction())
				{
					transaction_lock=true;
				}
				sqlite3_busy_timeout(db, 10000);
			}
			else
			{
				Server->Log("DATABASE BUSY in CDatabase::Prepare", LL_ERROR);
			}
		}
	}

	if(transaction_lock)
	{
		UnlockForTransaction();
		sqlite3_busy_timeout(db, 50);
	}

	if( err!=SQLITE_OK )
	{
		Server->Log("Error preparing Query ["+pQuery+"]: "+sqlite3_errmsg(db),LL_ERROR);
		return NULL;
	}
	CQuery *q=new CQuery(pQuery, prepared_statement, this);
	if( autodestroy )
		queries.push_back(q);

	return q;
}

IQuery* CDatabase::Prepare(int id, std::string pQuery)
{
	std::map<int, IQuery*>::iterator iter=prepared_queries.find(id);
	if( iter!=prepared_queries.end() )
	{
		iter->second->Reset();
		return iter->second;
	}
	else
	{
		IQuery *q=Prepare(pQuery, false);
		prepared_queries.insert(std::pair<int, IQuery*>(id, q) );
		return q;
	}
}

void CDatabase::destroyQuery(IQuery *q)
{
	for(size_t i=0;i<queries.size();++i)
	{
		if( queries[i]==q )
		{
			CQuery *cq=(CQuery*)q;
			delete cq;
			queries.erase( queries.begin()+i);
			return;
		}
	}
	CQuery *cq=(CQuery*)q;
	delete cq;
}

void CDatabase::destroyAllQueries(void)
{
	for(size_t i=0;i<queries.size();++i)
	{
		CQuery *cq=(CQuery*)queries[i];
		delete cq;
	}
	queries.clear();
}

_i64 CDatabase::getLastInsertID(void)
{
	return sqlite3_last_insert_rowid(db);
}

bool CDatabase::WaitForUnlock(void)
{
	int rc;
	UnlockNotification un;
	un.fired = false;
	un.mutex=Server->createMutex();
	un.cond=Server->createCondition();

	rc = sqlite3_unlock_notify(db, unlock_notify_cb, (void *)&un);

	if( rc==SQLITE_OK )
	{
		IScopedLock lock(un.mutex);
		if( !un.fired )
		{
			un.cond->wait(&lock);
		}
	}
	
	Server->destroy(un.mutex);
	Server->destroy(un.cond);

	return rc==SQLITE_OK;
}

sqlite3 *CDatabase::getDatabase(void)
{
	return db;
}

bool CDatabase::LockForTransaction(void)
{
	return lock_mutex->TryLock();
}

void CDatabase::UnlockForTransaction(void)
{
	lock_mutex->Unlock();
}