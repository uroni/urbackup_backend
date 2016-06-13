#ifndef INTERFACE_DATABASE_H
#define INTERFACE_DATABASE_H

#include <vector>
#include <string>
#include <map>
#include "Query.h"
#include "Object.h"

class IDatabase : public IObject
{
public:
	virtual db_results Read(std::string pQuery)=0; 
	virtual bool Write(std::string pQuery)=0;

	virtual bool BeginReadTransaction()=0;
	virtual bool BeginWriteTransaction()=0;
	virtual bool EndTransaction(void)=0;
	virtual bool RollbackTransaction(void) = 0;

	virtual IQuery* Prepare(std::string pQuery, bool autodestroy=true)=0;
	virtual void destroyQuery(IQuery *q)=0;
	virtual void destroyAllQueries(void)=0;

	virtual _i64 getLastInsertID(void)=0;

	virtual bool Import(const std::string &pFile)=0;
	virtual bool Dump(const std::string &pFile)=0;
	
	virtual std::string getEngineName(void)=0;

	virtual void DetachDBs(void)=0;
	virtual void AttachDBs(void)=0;

	class IBackupProgress
	{
	public:
		virtual void backupProgress(int64 pos, int64 total) = 0;
	};

	virtual bool Backup(const std::string &pFile, IBackupProgress* progress)=0;

	virtual void freeMemory()=0;

	virtual int getLastChanges()=0;

	virtual std::string getTempDirectoryPath() = 0;

	virtual void lockForSingleUse() = 0;

	virtual void unlockForSingleUse() = 0;
};

class DBScopedFreeMemory
{
public:
	DBScopedFreeMemory(IDatabase* db)
		: db(db) {}
	~DBScopedFreeMemory() {
		db->freeMemory();
	}
private:
	IDatabase* db;
};

class DBScopedDetach
{
public:
	DBScopedDetach(IDatabase* db)
		: db(db) {
			if(db!=NULL) db->DetachDBs();
	}
	~DBScopedDetach() {
		if(db!=NULL) db->AttachDBs();
	}
	void attach() {
		if(db!=NULL) db->AttachDBs();
		db=NULL;
	}
private:
	IDatabase* db;
};

class DBScopedAttach
{
public:
	DBScopedAttach(IDatabase* db)
		: db(db) {
			if(db!=NULL) db->AttachDBs();
	}
	~DBScopedAttach() {
		if(db!=NULL) db->DetachDBs();
	}
	void detach() {
		if(db!=NULL) db->DetachDBs();
		db=NULL;
	}
private:
	IDatabase* db;
};

class DBScopedWriteTransaction
{
public:
	DBScopedWriteTransaction(IDatabase* db)
		: db(db) {
			if(db!=NULL) db->BeginWriteTransaction();
	}
	~DBScopedWriteTransaction() {
		if(db!=NULL) db->EndTransaction();
	}

	void reset(IDatabase* pdb)
	{
		if (db != NULL) {
			db->EndTransaction();
		}
		db = pdb;
		if (db != NULL) {
			db->BeginWriteTransaction();
		}
	}

	void restart() {
		if(db!=NULL) {
			db->EndTransaction();
			db->BeginWriteTransaction();
		}
	}

	void rollback() {
		if (db != NULL) {
			db->RollbackTransaction();
		}
	}

	void end() {
		if(db!=NULL) db->EndTransaction();
		db=NULL;
	}
private:
	IDatabase* db;
};

class DBScopedSynchronous
{
public:
	DBScopedSynchronous(IDatabase* pdb)
		:db(NULL)
	{
		reset(pdb);
	}

	~DBScopedSynchronous()
	{
		if (db != NULL)
		{
			db->Write("PRAGMA synchronous = " + synchronous);
		}
	}

	void reset(IDatabase* pdb)
	{
		if (db != NULL)
		{
			db->Write("PRAGMA synchronous = " + synchronous);
		}
		db = pdb;
		if (db != NULL)
		{
			synchronous = db->Read("PRAGMA synchronous")[0]["synchronous"];
			db->Write("PRAGMA synchronous=FULL");
		}
	}

private:
	IDatabase* db;
	std::string synchronous;
};

#endif
