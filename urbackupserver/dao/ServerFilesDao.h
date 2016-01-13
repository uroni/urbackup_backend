#pragma once
#include "../../Interface/Database.h"

class ServerFilesDao
{
public:
	ServerFilesDao(IDatabase *db);
	~ServerFilesDao();


	int64 getLastId();
	int getLastChanges();
	void BeginWriteTransaction();
	void endTransaction();
	IDatabase* getDatabase();

	static const int c_direction_outgoing;
	static const int c_direction_outgoing_nobackupstat;
	static const int c_direction_incoming;


	//@-SQLGenFunctionsBegin
	struct CondInt64
	{
		bool exists;
		int64 value;
	};
	struct SFindFileEntry
	{
		bool exists;
		int64 id;
		std::string shahash;
		int backupid;
		int clientid;
		std::string fullpath;
		std::string hashpath;
		int64 filesize;
		int64 next_entry;
		int64 prev_entry;
		int64 rsize;
		int incremental;
		int pointed_to;
	};
	struct SIncomingStat
	{
		int64 id;
		int64 filesize;
		int clientid;
		int backupid;
		std::string existing_clients;
		int direction;
		int incremental;
	};
	struct SStatFileEntry
	{
		bool exists;
		int64 id;
		int backupid;
		int clientid;
		int64 filesize;
		int64 rsize;
		std::string shahash;
		int64 next_entry;
		int64 prev_entry;
	};


	void setNextEntry(int64 next_entry, int64 id);
	void setPrevEntry(int64 prev_entry, int64 id);
	void setPointedTo(int64 pointed_to, int64 id);
	CondInt64 getPointedTo(int64 id);
	void delFileEntry(int64 id);
	SFindFileEntry getFileEntry(int64 id);
	SStatFileEntry getStatFileEntry(int64 id);
	void addFileEntry(int backupid, const std::string& fullpath, const std::string& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to);
	bool createTemporaryPathLookupTable(void);
	void dropTemporaryPathLookupTable(void);
	void dropTemporaryPathLookupIndex(void);
	void populateTemporaryPathLookupTable(int backupid);
	bool createTemporaryPathLookupIndex(void);
	CondInt64 lookupEntryIdByPath(const std::string& fullpath);
	void addIncomingFile(int64 filesize, int clientid, int backupid, const std::string& existing_clients, int direction, int incremental);
	CondInt64 getIncomingStatsCount(void);
	void delIncomingStatEntry(int64 id);
	std::vector<SIncomingStat> getIncomingStats(void);
	void deleteFiles(int backupid);
	void removeDanglingFiles(void);
	//@-SQLGenFunctionsEnd

	int64 addFileEntryExternal(int backupid, const std::string& fullpath, const std::string& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to);

private:
	ServerFilesDao(ServerFilesDao& other) {}
	void operator=(ServerFilesDao& other) {}

	void prepareQueries();
	void destroyQueries();

	//@-SQLGenVariablesBegin
	IQuery* q_setNextEntry;
	IQuery* q_setPrevEntry;
	IQuery* q_setPointedTo;
	IQuery* q_getPointedTo;
	IQuery* q_delFileEntry;
	IQuery* q_getFileEntry;
	IQuery* q_getStatFileEntry;
	IQuery* q_addFileEntry;
	IQuery* q_createTemporaryPathLookupTable;
	IQuery* q_dropTemporaryPathLookupTable;
	IQuery* q_dropTemporaryPathLookupIndex;
	IQuery* q_populateTemporaryPathLookupTable;
	IQuery* q_createTemporaryPathLookupIndex;
	IQuery* q_lookupEntryIdByPath;
	IQuery* q_addIncomingFile;
	IQuery* q_getIncomingStatsCount;
	IQuery* q_delIncomingStatEntry;
	IQuery* q_getIncomingStats;
	IQuery* q_deleteFiles;
	IQuery* q_removeDanglingFiles;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
