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
	int getLastChanges();
	void attachDbs();
	void beginTransaction();
	void endTransaction();

	static const int c_direction_outgoing;
	static const int c_direction_outgoing_nobackupstat;
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
	struct SFileBackupInfo
	{
		bool exists;
		int64 id;
		int clientid;
		int64 backuptime;
		int incremental;
		std::wstring path;
		int complete;
		int64 running;
		int64 size_bytes;
		int done;
		int archived;
		int64 archive_timeout;
		int64 size_calculated;
		int resumed;
		int64 indexing_time_ms;
		int tgroup;
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
		int pointed_to;
	};
	struct SImageBackup
	{
		bool exists;
		int64 id;
		int incremental;
		std::wstring path;
		int64 duration;
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
	struct SLastIncremental
	{
		bool exists;
		int incremental;
		std::wstring path;
		int resumed;
		int complete;
		int id;
	};
	struct SReportSettings
	{
		bool exists;
		std::wstring report_mail;
		int report_loglevel;
		int report_sendonly;
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
	void setPointedTo(int64 pointed_to, int64 id);
	CondString getClientSetting(const std::wstring& key, int clientid);
	std::vector<int> getClientIds(void);
	CondInt64 getPointedTo(int64 id);
	void addFileEntry(int backupid, const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to);
	CondString getSetting(int clientid, const std::wstring& key);
	void insertSetting(const std::wstring& key, const std::wstring& value, int clientid);
	void updateSetting(const std::wstring& value, const std::wstring& key, int clientid);
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
	bool createTemporaryPathLookupTable(void);
	void dropTemporaryPathLookupTable(void);
	void dropTemporaryPathLookupIndex(void);
	void populateTemporaryPathLookupTable(int backupid);
	bool createTemporaryPathLookupIndex(void);
	CondInt64 lookupEntryIdByPath(const std::wstring& fullpath);
	void newFileBackup(int incremental, int clientid, const std::wstring& path, int resumed, int64 indexing_time_ms, int tgroup);
	void updateFileBackupRunning(int backupid);
	void setFileBackupDone(int backupid);
	SLastIncremental getLastIncrementalFileBackup(int clientid, int tgroup);
	SLastIncremental getLastIncrementalCompleteFileBackup(int clientid, int tgroup);
	void updateFileBackupSetComplete(int backupid);
	void saveBackupLog(int clientid, int errors, int warnings, int infos, int image, int incremental, int resumed, int restore);
	void saveBackupLogData(int64 logid, const std::wstring& data);
	std::vector<int> getMailableUserIds(void);
	CondString getUserRight(int clientid, const std::wstring& t_domain);
	SReportSettings getUserReportSettings(int userid);
	CondString formatUnixtime(int64 unixtime);
	SImageBackup getLastFullImage(int clientid, int image_version, const std::wstring& letter);
	SImageBackup getLastImage(int clientid, int image_version, const std::wstring& letter);
	void newImageBackup(int clientid, const std::wstring& path, int incremental, int incremental_ref, int image_version, const std::wstring& letter);
	void setImageSize(int64 size_bytes, int backupid);
	void addImageSizeToClient(int clientid, int64 add_size);
	void setImageBackupComplete(int backupid);
	void updateImageBackupRunning(int backupid);
	void saveImageAssociation(int img_id, int assoc_id);
	void updateClientLastImageBackup(int backupid, int clientid);
	void updateClientLastFileBackup(int backupid, int clientid);
	void deleteAllUsersOnClient(int clientid);
	void addUserOnClient(int clientid, const std::wstring& username);
	void addClientToken(int clientid, const std::wstring& token);
	void addUserToken(const std::wstring& username, const std::wstring& token);
	CondInt64 hasRecentFullOrIncrFileBackup(const std::wstring& backup_interval_full, int clientid, const std::wstring& backup_interval_incr);
	CondInt64 hasRecentIncrFileBackup(const std::wstring& backup_interval, int clientid);
	CondInt64 hasRecentFullOrIncrImageBackup(const std::wstring& backup_interval_full, int clientid, const std::wstring& backup_interval_incr, int image_version, const std::wstring& letter);
	CondInt64 hasRecentIncrImageBackup(const std::wstring& backup_interval, int clientid, int image_version, const std::wstring& letter);
	void addRestore(int clientid, const std::wstring& path, const std::wstring& identity);
	CondString getRestoreIdentity(int64 restore_id, int clientid);
	void setRestoreDone(int success, int64 restore_id);
	SFileBackupInfo getFileBackupInfo(int backupid);
	//@-SQLGenFunctionsEnd

	int64 addFileEntryExternal(int backupid, const std::wstring& fullpath, const std::wstring& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to);
	void updateOrInsertSetting(int clientid, const std::wstring& key, const std::wstring& value);

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
	IQuery* q_setPointedTo;
	IQuery* q_getClientSetting;
	IQuery* q_getClientIds;
	IQuery* q_getPointedTo;
	IQuery* q_addFileEntry;
	IQuery* q_getSetting;
	IQuery* q_insertSetting;
	IQuery* q_updateSetting;
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
	IQuery* q_createTemporaryPathLookupTable;
	IQuery* q_dropTemporaryPathLookupTable;
	IQuery* q_dropTemporaryPathLookupIndex;
	IQuery* q_populateTemporaryPathLookupTable;
	IQuery* q_createTemporaryPathLookupIndex;
	IQuery* q_lookupEntryIdByPath;
	IQuery* q_newFileBackup;
	IQuery* q_updateFileBackupRunning;
	IQuery* q_setFileBackupDone;
	IQuery* q_getLastIncrementalFileBackup;
	IQuery* q_getLastIncrementalCompleteFileBackup;
	IQuery* q_updateFileBackupSetComplete;
	IQuery* q_saveBackupLog;
	IQuery* q_saveBackupLogData;
	IQuery* q_getMailableUserIds;
	IQuery* q_getUserRight;
	IQuery* q_getUserReportSettings;
	IQuery* q_formatUnixtime;
	IQuery* q_getLastFullImage;
	IQuery* q_getLastImage;
	IQuery* q_newImageBackup;
	IQuery* q_setImageSize;
	IQuery* q_addImageSizeToClient;
	IQuery* q_setImageBackupComplete;
	IQuery* q_updateImageBackupRunning;
	IQuery* q_saveImageAssociation;
	IQuery* q_updateClientLastImageBackup;
	IQuery* q_updateClientLastFileBackup;
	IQuery* q_deleteAllUsersOnClient;
	IQuery* q_addUserOnClient;
	IQuery* q_addClientToken;
	IQuery* q_addUserToken;
	IQuery* q_hasRecentFullOrIncrFileBackup;
	IQuery* q_hasRecentIncrFileBackup;
	IQuery* q_hasRecentFullOrIncrImageBackup;
	IQuery* q_hasRecentIncrImageBackup;
	IQuery* q_addRestore;
	IQuery* q_getRestoreIdentity;
	IQuery* q_setRestoreDone;
	IQuery* q_getFileBackupInfo;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
