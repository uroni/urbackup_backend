#pragma once
#include "../../Interface/Database.h"

class ServerBackupDao
{
public:
	ServerBackupDao(IDatabase *db);
	~ServerBackupDao();


	void commit();
	int64 getLastId();
	void detachDbs();
	void attachDbs();
	void beginTransaction();
	void endTransaction();

	static const int c_direction_outgoing;
	static const int c_direction_incoming;


	//@-SQLGenFunctionsBegin
	struct CondInt64
	{
		bool exists;
		int64 value;
	};
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
		int64 rsize;
	};
	struct SFindFileEntry
	{
		bool exists;
		int64 id;
		std::wstring shahash;
		int backupid;
		int clientid;
		std::wstring fullpath;
		std::wstring hashpath;
		int64 filesize;
		int64 next_entry;
		int64 prev_entry;
		int64 rsize;
		int incremental;
	};
	struct SIncomingStat
	{
		int64 id;
		int64 filesize;
		int clientid;
		int backupid;
		std::wstring existing_clients;
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
		std::wstring shahash;
		int64 next_entry;
		int64 prev_entry;
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
	void insertIntoOrigClientSettings(int clientid, std::string data);
	CondString getOrigClientSettings(int clientid);
	std::vector<SDuration> getLastIncrementalDurations(int clientid);
	std::vector<SDuration> getLastFullDurations(int clientid);
	void setNextEntry(int64 next_entry, int64 id);
	void setPrevEntry(int64 prev_entry, int64 id);
	void addFileEntry(int backupid, const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 prev_entry);
	void delFileEntry(int64 id);
	SFindFileEntry getFileEntry(int64 id);
	SStatFileEntry getStatFileEntry(int64 id);
	void addIncomingFile(int64 filesize, int clientid, int backupid, const std::wstring& existing_clients, int direction, int incremental);
	std::vector<SIncomingStat> getIncomingStats(void);
	CondInt64 getIncomingStatsCount(void);
	void delIncomingStatEntry(int64 id);
	CondString getMiscValue(const std::wstring& tkey);
	void addMiscValue(const std::wstring& tkey, const std::wstring& tvalue);
	void delMiscValue(const std::wstring& tkey);
	void setClientUsedFilebackupSize(int64 bytes_used_files, int id);
	//@-SQLGenFunctionsEnd

	int64 addFileEntryExternal(int backupid, const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 prev_entry);

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
	IQuery* q_insertIntoOrigClientSettings;
	IQuery* q_getOrigClientSettings;
	IQuery* q_getLastIncrementalDurations;
	IQuery* q_getLastFullDurations;
	IQuery* q_setNextEntry;
	IQuery* q_setPrevEntry;
	IQuery* q_addFileEntry;
	IQuery* q_delFileEntry;
	IQuery* q_getFileEntry;
	IQuery* q_getStatFileEntry;
	IQuery* q_addIncomingFile;
	IQuery* q_getIncomingStats;
	IQuery* q_getIncomingStatsCount;
	IQuery* q_delIncomingStatEntry;
	IQuery* q_getMiscValue;
	IQuery* q_addMiscValue;
	IQuery* q_delMiscValue;
	IQuery* q_setClientUsedFilebackupSize;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
