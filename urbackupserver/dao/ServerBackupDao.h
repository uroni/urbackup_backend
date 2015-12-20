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
	void BeginWriteTransaction();
	void endTransaction();

	static const int c_direction_outgoing;
	static const int c_direction_outgoing_nobackupstat;
	static const int c_direction_incoming;


	//@-SQLGenFunctionsBegin
	struct CondInt
	{
		bool exists;
		int value;
	};
	struct CondInt64
	{
		bool exists;
		int64 value;
	};
	struct CondString
	{
		bool exists;
		std::string value;
	};
	struct DirectoryLinkEntry
	{
		std::string name;
		std::string target;
	};
	struct JournalEntry
	{
		std::string linkname;
		std::string linktarget;
	};
	struct SClientName
	{
		bool exists;
		std::string virtualmain;
		std::string name;
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
		std::string path;
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
	struct SImageBackup
	{
		bool exists;
		int64 id;
		int incremental;
		std::string path;
		int64 duration;
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
	struct SLastIncremental
	{
		bool exists;
		int incremental;
		std::string path;
		int resumed;
		int complete;
		int id;
	};
	struct SReportSettings
	{
		bool exists;
		std::string report_mail;
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
		std::string shahash;
		int64 next_entry;
		int64 prev_entry;
	};


	void addDirectoryLink(int clientid, const std::string& name, const std::string& target);
	void removeDirectoryLink(int clientid, const std::string& target);
	void removeDirectoryLinkGlob(int clientid, const std::string& target);
	int getDirectoryRefcount(int clientid, const std::string& name);
	void addDirectoryLinkJournalEntry(const std::string& linkname, const std::string& linktarget);
	void removeDirectoryLinkJournalEntry(int64 entry_id);
	std::vector<JournalEntry> getDirectoryLinkJournalEntries(void);
	void removeDirectoryLinkJournalEntries(void);
	std::vector<DirectoryLinkEntry> getLinksInDirectory(int clientid, const std::string& dir);
	void deleteLinkReferenceEntry(int64 id);
	void updateLinkReferenceTarget(const std::string& new_target, int64 id);
	void addToOldBackupfolders(const std::string& backupfolder);
	std::vector<std::string> getOldBackupfolders(void);
	std::vector<std::string> getDeletePendingClientNames(void);
	SClientName getVirtualMainClientname(int clientid);
	void insertIntoOrigClientSettings(int clientid, const std::string& data);
	CondString getOrigClientSettings(int clientid);
	std::vector<SDuration> getLastIncrementalDurations(int clientid);
	std::vector<SDuration> getLastFullDurations(int clientid);
	void setNextEntry(int64 next_entry, int64 id);
	void setPrevEntry(int64 prev_entry, int64 id);
	void setPointedTo(int64 pointed_to, int64 id);
	CondString getClientSetting(const std::string& key, int clientid);
	std::vector<int> getClientIds(void);
	CondInt64 getPointedTo(int64 id);
	void addFileEntry(int backupid, const std::string& fullpath, const std::string& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to);
	CondString getSetting(int clientid, const std::string& key);
	void insertSetting(const std::string& key, const std::string& value, int clientid);
	void updateSetting(const std::string& value, const std::string& key, int clientid);
	void delFileEntry(int64 id);
	SFindFileEntry getFileEntry(int64 id);
	SStatFileEntry getStatFileEntry(int64 id);
	void addIncomingFile(int64 filesize, int clientid, int backupid, const std::string& existing_clients, int direction, int incremental);
	std::vector<SIncomingStat> getIncomingStats(void);
	CondInt64 getIncomingStatsCount(void);
	void delIncomingStatEntry(int64 id);
	CondString getMiscValue(const std::string& tkey);
	void addMiscValue(const std::string& tkey, const std::string& tvalue);
	void delMiscValue(const std::string& tkey);
	void setClientUsedFilebackupSize(int64 bytes_used_files, int id);
	bool createTemporaryPathLookupTable(void);
	void dropTemporaryPathLookupTable(void);
	void dropTemporaryPathLookupIndex(void);
	void populateTemporaryPathLookupTable(int backupid);
	bool createTemporaryPathLookupIndex(void);
	CondInt64 lookupEntryIdByPath(const std::string& fullpath);
	void newFileBackup(int incremental, int clientid, const std::string& path, int resumed, int64 indexing_time_ms, int tgroup);
	void updateFileBackupRunning(int backupid);
	void setFileBackupDone(int backupid);
	SLastIncremental getLastIncrementalFileBackup(int clientid, int tgroup);
	SLastIncremental getLastIncrementalCompleteFileBackup(int clientid, int tgroup);
	void updateFileBackupSetComplete(int backupid);
	void saveBackupLog(int clientid, int errors, int warnings, int infos, int image, int incremental, int resumed, int restore);
	void saveBackupLogData(int64 logid, const std::string& data);
	std::vector<int> getMailableUserIds(void);
	CondString getUserRight(int clientid, const std::string& t_domain);
	SReportSettings getUserReportSettings(int userid);
	CondString formatUnixtime(int64 unixtime);
	SImageBackup getLastFullImage(int clientid, int image_version, const std::string& letter);
	SImageBackup getLastImage(int clientid, int image_version, const std::string& letter);
	void newImageBackup(int clientid, const std::string& path, int incremental, int incremental_ref, int image_version, const std::string& letter);
	void setImageSize(int64 size_bytes, int backupid);
	void addImageSizeToClient(int clientid, int64 add_size);
	void setImageBackupComplete(int backupid);
	void updateImageBackupRunning(int backupid);
	void saveImageAssociation(int img_id, int assoc_id);
	void updateClientLastImageBackup(int backupid, int clientid);
	void updateClientLastFileBackup(int backupid, int clientid);
	void deleteAllUsersOnClient(int clientid);
	void addUserOnClient(int clientid, const std::string& username);
	void addClientToken(int clientid, const std::string& token);
	void addUserToken(const std::string& username, const std::string& token);
	CondInt64 hasRecentFullOrIncrFileBackup(const std::string& backup_interval_full, int clientid, const std::string& backup_interval_incr, int tgroup);
	CondInt64 hasRecentIncrFileBackup(const std::string& backup_interval, int clientid, int tgroup);
	CondInt64 hasRecentFullOrIncrImageBackup(const std::string& backup_interval_full, int clientid, const std::string& backup_interval_incr, int image_version, const std::string& letter);
	CondInt64 hasRecentIncrImageBackup(const std::string& backup_interval, int clientid, int image_version, const std::string& letter);
	void addRestore(int clientid, const std::string& path, const std::string& identity);
	CondString getRestoreIdentity(int64 restore_id, int clientid);
	void setRestoreDone(int success, int64 restore_id);
	SFileBackupInfo getFileBackupInfo(int backupid);
	void setVirtualMainClient(const std::string& virtualmain, int64 clientid);
	void deleteUsedAccessTokens(int clientid);
	CondInt hasUsedAccessToken(const std::string& tokenhash);
	void addUsedAccessToken(int clientid, const std::string& tokenhash);
	//@-SQLGenFunctionsEnd

	int64 addFileEntryExternal(int backupid, const std::string& fullpath, const std::string& hashpath, const std::string& shahash, int64 filesize, int64 rsize, int clientid, int incremental, int64 next_entry, int64 prev_entry, int pointed_to);
	void updateOrInsertSetting(int clientid, const std::string& key, const std::string& value);

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
	IQuery* q_getVirtualMainClientname;
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
	IQuery* q_setVirtualMainClient;
	IQuery* q_deleteUsedAccessTokens;
	IQuery* q_hasUsedAccessToken;
	IQuery* q_addUsedAccessToken;
	//@-SQLGenVariablesEnd

	IDatabase *db;
};
