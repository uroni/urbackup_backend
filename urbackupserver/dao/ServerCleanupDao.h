#pragma once
#include "../../Interface/Database.h"

class ServerCleanupDao
{
public:

	ServerCleanupDao(IDatabase *db);
	~ServerCleanupDao(void);

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
		std::wstring value;
	};
	struct SClientInfo
	{
		int id;
		std::wstring name;
	};
	struct SFileBackupInfo
	{
		bool exists;
		int id;
		std::wstring backuptime;
		std::wstring path;
	};
	struct SHistItem
	{
		int id;
		std::wstring name;
		std::wstring lastbackup;
		std::wstring lastseen;
		std::wstring lastbackup_image;
		int64 bytes_used_files;
		int64 bytes_used_images;
		std::wstring max_created;
		int64 hist_id;
	};
	struct SImageBackupInfo
	{
		bool exists;
		int id;
		std::wstring backuptime;
		std::wstring path;
		std::wstring letter;
	};
	struct SImageLetter
	{
		int id;
		std::wstring letter;
	};
	struct SImageRef
	{
		int id;
		int complete;
	};
	struct SIncompleteFileBackup
	{
		int id;
		int clientid;
		int incremental;
		std::wstring backuptime;
		std::wstring path;
		std::wstring clientname;
	};
	struct SIncompleteImages
	{
		int id;
		std::wstring path;
	};


	std::vector<SIncompleteImages> getIncompleteImages(void);
	void removeImage(int id);
	std::vector<int> getClientsSortFilebackups(void);
	std::vector<int> getClientsSortImagebackups(void);
	std::vector<SImageLetter> getFullNumImages(int clientid);
	std::vector<SImageRef> getImageRefs(int incremental_ref);
	CondString getImagePath(int id);
	std::vector<SImageLetter> getIncrNumImages(int clientid);
	int getIncrNumImagesForBackup(int backupid);
	std::vector<int> getFullNumFiles(int clientid);
	std::vector<int> getIncrNumFiles(int clientid);
	CondString getClientName(int clientid);
	CondString getFileBackupPath(int backupid);
	void deleteFiles(int backupid);
	void removeFileBackup(int backupid);
	SFileBackupInfo getFileBackupInfo(int backupid);
	SImageBackupInfo getImageBackupInfo(int backupid);
	void removeImageSize(int backupid);
	void addToImageStats(int64 size_correction, int backupid);
	void updateDelImageStats(int64 rowid);
	std::vector<SImageBackupInfo> getClientImages(int clientid);
	std::vector<int> getClientFileBackups(int clientid);
	CondInt getParentImageBackup(int assoc_id);
	std::vector<int> getAssocImageBackups(int img_id);
	CondInt64 getImageSize(int backupid);
	std::vector<SClientInfo> getClients(void);
	std::vector<SFileBackupInfo> getFileBackupsOfClient(int clientid);
	std::vector<SImageBackupInfo> getImageBackupsOfClient(int clientid);
	CondInt findFileBackup(int clientid, const std::wstring& path);
	void removeDanglingFiles(void);
	CondInt64 getUsedStorage(int clientid);
	void cleanupBackupLogs(void);
	void cleanupAuthLog(void);
	std::vector<SIncompleteFileBackup> getIncompleteFileBackups(void);
	std::vector<SHistItem> getClientHistory(const std::wstring& back_start, const std::wstring& back_stop, const std::wstring& date_grouping);
	void deleteClientHistoryIds(const std::wstring& back_start, const std::wstring& back_stop);
	void deleteClientHistoryItems(const std::wstring& back_start, const std::wstring& back_stop);
	void insertClientHistoryId(const std::wstring& created);
	void insertClientHistoryItem(int id, const std::wstring& name, const std::wstring& lastbackup, const std::wstring& lastseen, const std::wstring& lastbackup_image, int64 bytes_used_files, int64 bytes_used_images, const std::wstring& created, int64 hist_id);
	//@-SQLGenFunctionsEnd

private:
	void createQueries(void);
	void destroyQueries(void);

	IDatabase *db;

	//@-SQLGenVariablesBegin
	IQuery* q_getIncompleteImages;
	IQuery* q_removeImage;
	IQuery* q_getClientsSortFilebackups;
	IQuery* q_getClientsSortImagebackups;
	IQuery* q_getFullNumImages;
	IQuery* q_getImageRefs;
	IQuery* q_getImagePath;
	IQuery* q_getIncrNumImages;
	IQuery* q_getIncrNumImagesForBackup;
	IQuery* q_getFullNumFiles;
	IQuery* q_getIncrNumFiles;
	IQuery* q_getClientName;
	IQuery* q_getFileBackupPath;
	IQuery* q_deleteFiles;
	IQuery* q_removeFileBackup;
	IQuery* q_getFileBackupInfo;
	IQuery* q_getImageBackupInfo;
	IQuery* q_removeImageSize;
	IQuery* q_addToImageStats;
	IQuery* q_updateDelImageStats;
	IQuery* q_getClientImages;
	IQuery* q_getClientFileBackups;
	IQuery* q_getParentImageBackup;
	IQuery* q_getAssocImageBackups;
	IQuery* q_getImageSize;
	IQuery* q_getClients;
	IQuery* q_getFileBackupsOfClient;
	IQuery* q_getImageBackupsOfClient;
	IQuery* q_findFileBackup;
	IQuery* q_removeDanglingFiles;
	IQuery* q_getUsedStorage;
	IQuery* q_cleanupBackupLogs;
	IQuery* q_cleanupAuthLog;
	IQuery* q_getIncompleteFileBackups;
	IQuery* q_getClientHistory;
	IQuery* q_deleteClientHistoryIds;
	IQuery* q_deleteClientHistoryItems;
	IQuery* q_insertClientHistoryId;
	IQuery* q_insertClientHistoryItem;
	//@-SQLGenVariablesEnd
};