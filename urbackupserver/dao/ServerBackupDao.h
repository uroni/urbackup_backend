#pragma once
#include "../../Interface/Database.h"

class ServerBackupDao
{
public:
	ServerBackupDao(IDatabase *db);
	~ServerBackupDao();


	void commit();
	int64 getLastId();
	void beginTransaction();
	void endTransaction();


	//@-SQLGenFunctionsBegin
	struct CondString
	{
		bool exists;
		std::wstring value;
	};
	struct DirectoryLinkEntry
	{
		std::wstring name;
		std::wstring target;
	};
	struct JournalEntry
	{
		std::wstring linkname;
		std::wstring linktarget;
	};
	struct SDuration
	{
		int64 indexing_time_ms;
		int64 duration;
	};
	struct SFileEntry
	{
		bool exists;
		std::wstring fullpath;
		std::wstring hashpath;
		std::string shahash;
		int64 filesize;
	};


	void addDirectoryLink(int clientid, const std::wstring& name, const std::wstring& target);
	void removeDirectoryLink(int clientid, const std::wstring& target);
	void removeDirectoryLinkGlob(int clientid, const std::wstring& target);
	int getDirectoryRefcount(int clientid, const std::wstring& name);
	void addDirectoryLinkJournalEntry(const std::wstring& linkname, const std::wstring& linktarget);
	void removeDirectoryLinkJournalEntry(int64 entry_id);
	std::vector<JournalEntry> getDirectoryLinkJournalEntries(void);
	void removeDirectoryLinkJournalEntries(void);
	std::vector<DirectoryLinkEntry> getLinksInDirectory(int clientid, const std::wstring& dir);
	void deleteLinkReferenceEntry(int64 id);
	void updateLinkReferenceTarget(const std::wstring& new_target, int64 id);
	void addToOldBackupfolders(const std::wstring& backupfolder);
	std::vector<std::wstring> getOldBackupfolders(void);
	std::vector<std::wstring> getDeletePendingClientNames(void);
	bool createTemporaryLastFilesTable(void);
	void dropTemporaryLastFilesTable(void);
	bool createTemporaryLastFilesTableIndex(void);
	bool dropTemporaryLastFilesTableIndex(void);
	bool copyToTemporaryLastFilesTable(int backupid);
	SFileEntry getFileEntryFromTemporaryTable(const std::wstring& fullpath);
	std::vector<SFileEntry> getFileEntriesFromTemporaryTableGlob(const std::wstring& fullpath_glob);
	bool createTemporaryNewFilesTable(void);
	void dropTemporaryNewFilesTable(void);
	void insertIntoTemporaryNewFilesTable(const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize);
	void copyFromTemporaryNewFilesTableToFilesTable(int backupid, int clientid, int incremental);
	void copyFromTemporaryNewFilesTableToFilesNewTable(int backupid, int clientid, int incremental);
	void insertIntoOrigClientSettings(int clientid, std::string data);
	CondString getOrigClientSettings(int clientid);
	std::vector<SDuration> getLastIncrementalDurations(int clientid);
	std::vector<SDuration> getLastFullDurations(int clientid);
	CondString getClientSetting(const std::wstring& key, int clientid);
	std::vector<int> getClientIds(void);
	//@-SQLGenFunctionsEnd

private:
	ServerBackupDao(ServerBackupDao& other) {}
	void operator=(ServerBackupDao& other) {}

	void prepareQueries(void);
	void destroyQueries(void);

	//@-SQLGenVariablesBegin
	IQuery* q_addDirectoryLink;
	IQuery* q_removeDirectoryLink;
	IQuery* q_removeDirectoryLinkGlob;
	IQuery* q_getDirectoryRefcount;
	IQuery* q_addDirectoryLinkJournalEntry;
	IQuery* q_removeDirectoryLinkJournalEntry;
	IQuery* q_getDirectoryLinkJournalEntries;
	IQuery* q_removeDirectoryLinkJournalEntries;
	IQuery* q_getLinksInDirectory;
	IQuery* q_deleteLinkReferenceEntry;
	IQuery* q_updateLinkReferenceTarget;
	IQuery* q_addToOldBackupfolders;
	IQuery* q_getOldBackupfolders;
	IQuery* q_getDeletePendingClientNames;
	IQuery* q_createTemporaryLastFilesTable;
	IQuery* q_dropTemporaryLastFilesTable;
	IQuery* q_createTemporaryLastFilesTableIndex;
	IQuery* q_dropTemporaryLastFilesTableIndex;
	IQuery* q_copyToTemporaryLastFilesTable;
	IQuery* q_getFileEntryFromTemporaryTable;
	IQuery* q_getFileEntriesFromTemporaryTableGlob;
	IQuery* q_createTemporaryNewFilesTable;
	IQuery* q_dropTemporaryNewFilesTable;
	IQuery* q_insertIntoTemporaryNewFilesTable;
	IQuery* q_copyFromTemporaryNewFilesTableToFilesTable;
	IQuery* q_copyFromTemporaryNewFilesTableToFilesNewTable;
	IQuery* q_insertIntoOrigClientSettings;
	IQuery* q_getOrigClientSettings;
	IQuery* q_getLastIncrementalDurations;
	IQuery* q_getLastFullDurations;
	IQuery* q_getClientSetting;
	IQuery* q_getClientIds;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
