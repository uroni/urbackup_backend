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
#ifndef NO_SQLITE

#if defined(_WIN32) || defined(WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "vld.h"
#ifndef BDBPLUGIN
#include "Server.h"
#else
#ifdef LINUX
#include "bdbplugin/config.h"
#include DB_HEADER
#else
#include <db.h>
#endif
#include "Interface/Server.h"
#endif
#include "Query.h"
#include "sqlite/sqlite3.h"
#include "Interface/File.h"
extern "C"
{
	#include "sqlite/shell.h"
}
#include "Database.h"
#include "stringtools.h"

IMutex * CDatabase::lock_mutex=NULL;
int CDatabase::lock_count=0;
ICondition *CDatabase::unlock_cond=NULL;


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

bool CDatabase::Open(std::string pFile, const std::vector<std::pair<std::string,std::string> > &attach)
{
	attached_dbs=attach;
	in_transaction=false;
	if( sqlite3_open(pFile.c_str(), &db) )
	{
		Server->Log("Could not open db ["+pFile+"]");
		sqlite3_close(db);
		return false;
	}
	else
	{
		
		#ifdef BDBPLUGIN
		/*db_results res=Read("PRAGMA multiversion");
		if(!res.empty() && res[0][L"multiversion"]!=L"1")
		{
		    Write("PRAGMA multiversion=ON");
		}*/
		Write("PRAGMA synchronous=ON");
		//Write("PRAGMA snapshot_isolation=ON");
		//Write("PRAGMA bdbsql_error_file='urbackup/bdb_errors.log'");
		#else
		Write("PRAGMA synchronous=NORMAL");
		#endif
		Write("PRAGMA foreign_keys = ON");
		sqlite3_busy_timeout(db, c_sqlite_busy_timeout_default);
		AttachDBs();

		return true;
	}
}

void CDatabase::initMutex(void)
{
	lock_mutex=Server->createMutex();
	unlock_cond=Server->createCondition();
}

void CDatabase::destroyMutex(void)
{
	Server->destroy(lock_mutex);
	Server->destroy(unlock_cond);
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
	if(q!=NULL)
	{
		db_results ret=q->Read();
		delete ((CQuery*)q);
		return ret;
	}
	return db_results();
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
	in_transaction=true;
}

bool CDatabase::EndTransaction(void)
{
	Write("END;");
	in_transaction=false;
	IScopedLock lock(lock_mutex);
	bool waited=false;
	while(lock_count>0)
	{
		unlock_cond->wait(&lock);
		waited=true;
	}
	if(waited)
	{
		Server->wait(50);
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
				if(!isInTransaction() && LockForTransaction())
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

		if(err==SQLITE_IOERR)
		{
			Server->setFailBit(IServer::FAIL_DATABASE_IOERR);
		}
		if(err==SQLITE_CORRUPT)
		{
			Server->setFailBit(IServer::FAIL_DATABASE_CORRUPTED);			
		}

		return NULL;
	}
	CQuery *q=new CQuery(pQuery, prepared_statement, this);
	if( autodestroy )
	{
		queries.push_back(q);
	}

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
	if(q==NULL)
	{
		return;
	}

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
#ifndef BDBPLUGIN
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
#else
	return false;
#endif
}

sqlite3 *CDatabase::getDatabase(void)
{
	return db;
}

bool CDatabase::LockForTransaction(void)
{
	lock_mutex->Lock();
	++lock_count;
	return true;
}

void CDatabase::UnlockForTransaction(void)
{
	--lock_count;
	unlock_cond->notify_all();
	lock_mutex->Unlock();
}

bool CDatabase::isInTransaction(void)
{
	return in_transaction;
}

bool CDatabase::Import(const std::string &pFile)
{
	IFile *file=Server->openFile(pFile, MODE_READ);
	if(file==NULL)
		return false;

	unsigned int r;
	char buf[4096];
	std::string query;
	int state=0;
	do
	{
		r=file->Read(buf, 4096);
		for(unsigned int i=0;i<r;++i)
		{
			if(buf[i]==';' && state==0)
			{
				if(!Write(query))
					return false;

				query.clear();
				continue;
			}
			else if(buf[i]=='\'' && state==0)
			{
				state=1;
			}
			else if(buf[i]=='\'' && state==1 )
			{
				state=0;
			}
			
			query+=buf[i];
		}
	}
	while(r>0);

	Server->destroy(file);
	return true;
}

bool CDatabase::Dump(const std::string &pFile)
{
	callback_data cd;
	cd.db=db;
	cd.out=fopen(pFile.c_str(), "wb");
	if(cd.out==0)
	{
		return false;
	}

	char *cmd=new char[6];
	cmd[0]='.'; cmd[1]='d'; cmd[2]='u'; cmd[3]='m'; cmd[4]='p'; cmd[5]=0;
	do_meta_command_r(cmd, &cd);
	delete []cmd;

	fclose(cd.out);

	return true;
}

std::string CDatabase::getEngineName(void)
{
	#ifndef BDBPLUGIN
	return "sqlite";
	#else
	return "bdb";
	#endif
}

void CDatabase::AttachDBs(void)
{
	for(size_t i=0;i<attached_dbs.size();++i)	{		Write("ATTACH DATABASE '"+attached_dbs[i].first+"' AS "+attached_dbs[i].second);	}
}

void CDatabase::DetachDBs(void)
{
	for(size_t i=0;i<attached_dbs.size();++i)	{		Write("DETACH DATABASE "+attached_dbs[i].second);	}
}

bool CDatabase::backup_db(const std::string &pFile, const std::string &pDB)
{
	int rc;                     /* Function return code */
  sqlite3 *pBackupDB;             /* Database connection opened on zFilename */
  sqlite3_backup *pBackup;    /* Backup handle used to copy data */

  /* Open the database file identified by zFilename. */
  rc = sqlite3_open(pFile.c_str(), &pBackupDB);
  if( rc==SQLITE_OK ){

    /* Open the sqlite3_backup object used to accomplish the transfer */
	  pBackup = sqlite3_backup_init(pBackupDB, "main", db, pDB.c_str());
    if( pBackup ){

      /* Each iteration of this loop copies 5 database pages from database
      ** pDb to the backup database. If the return value of backup_step()
      ** indicates that there are still further pages to copy, sleep for
      ** 250 ms before repeating. */
      do {
        rc = sqlite3_backup_step(pBackup, -1);

		if(rc!=SQLITE_OK)
			Server->wait(250);

      } while( rc==SQLITE_OK || rc==SQLITE_BUSY || rc==SQLITE_LOCKED );

      /* Release resources allocated by backup_init(). */
      (void)sqlite3_backup_finish(pBackup);
    }
	else
	{
		Server->Log("Opening backup connection failed", LL_INFO);
	}
    rc = sqlite3_errcode(pBackupDB);
	if(rc!=0)
	{
		Server->Log("Database backup failed with error code: "+nconvert(rc)+" err: "+sqlite3_errmsg(pBackupDB), LL_INFO);
	}
  }
  
  /* Close the database connection opened on database file zFilename
  ** and return the result of this function. */
  (void)sqlite3_close(pBackupDB);
  return rc==0;
}

bool CDatabase::Backup(const std::string &pFile)
{
	std::string path=ExtractFilePath(pFile);
	bool b=backup_db(pFile, "main");
	if(!b)
		return false;

	for(size_t i=0;i<attached_dbs.size();++i)
	{
		b=backup_db(path+"/"+ExtractFileName(attached_dbs[i].first), attached_dbs[i].second);
		if(!b)
			return false;
	}

	return true;
}

void CDatabase::freeMemory()
{
	sqlite3_db_release_memory(db);
}

int CDatabase::getLastChanges()
{
	return sqlite3_changes(db);
}

#endif //NO_SQLITE