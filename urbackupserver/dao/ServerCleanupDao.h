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
		std::string value;
	};
	struct SClientInfo
	{
		int id;
		std::string name;
	};
	struct SFileBackupInfo
	{
		bool exists;
		int id;
		std::string backuptime;
		std::string path;
		int done;
	};
	struct SHistItem
	{
		int id;
		std::string name;
		std::string lastbackup;
		std::string lastseen;
		std::string lastbackup_image;
		int64 bytes_used_files;
		int64 bytes_used_images;
		std::string max_created;
		int64 hist_id;
	};
	struct SImageBackupInfo
	{
		bool exists;
		int id;
		std::string backuptime;
		std::string path;
		std::string letter;
		int complete;
	};
	struct SImageLetter
	{
		int id;
		std::string letter;
	};
	struct SImageRef
	{
		int id;
		int complete;
		int archived;
	};
	struct SIncompleteFileBackup
	{
		int id;
		int clientid;
		int incremental;
		std::string backuptime;
		std::string path;
		std::string clientname;
	};
	struct SIncompleteImages
	{
		int id;
		std::string path;
		std::string clientname;
	};


	std::vector<SIncompleteImages> getIncompleteImages(void);
	std::vector<SIncompleteImages> getDeletePendingImages(void);
	void removeImage(int id);
	std::vector<int> getClientsSortFilebackups(void);
	std::vector<int> getClientsSortImagebackups(void);
	std::vector<SImageLetter> getFullNumImages(int clientid);
	std::vector<SImageRef> getImageRefs(int incremental_ref);
	std::vector<SImageRef> getImageRefsReverse(int backupid);
	CondInt getImageClientId(int id);
	CondInt getFileBackupClientId(int id);
	CondString getImageClientname(int id);
	CondString getImagePath(int id);
	std::vector<SImageLetter> getIncrNumImages(int clientid);
	int getIncrNumImagesForBackup(int backupid);
	std::vector<int> getFullNumFiles(int clientid);
	std::vector<int> getIncrNumFiles(int clientid);
	CondString getClientName(int clientid);
	CondString getFileBackupPath(int backupid);
	void removeFileBackup(int backupid);
	SFileBackupInfo getFileBackupInfo(int backupid);
	SImageBackupInfo getImageBackupInfo(int backupid);
	void removeImageSize(int backupid);
	void addToImageStats(int64 size_correction, int backupid);
	void updateDelImageStats(int64 rowid);
	std::vector<SImageBackupInfo> getClientImages(int clientid);
	std::vector<int> getClientFileBackups(int clientid);
	CondInt getParentImageBackup(int assoc_id);
	CondInt getImageArchived(int backupid);
	std::vector<int> getAssocImageBackups(int img_id);
	std::vector<int> getAssocImageBackupsReverse(int assoc_id);
	CondInt64 getImageSize(int backupid);
	std::vector<SClientInfo> getClients(void);
	std::vector<SFileBackupInfo> getFileBackupsOfClient(int clientid);
	std::vector<SImageBackupInfo> getOldImageBackupsOfClient(int clientid);
	std::vector<SImageBackupInfo> getImageBackupsOfClient(int clientid);
	CondInt findFileBackup(int clientid, const std::string& path);
	CondInt64 getUsedStorage(int clientid);
	void cleanupBackupLogs(void);
	void cleanupAuthLog(void);
	std::vector<SIncompleteFileBackup> getIncompleteFileBackups(void);
	std::vector<SIncompleteFileBackup> getDeletePendingFileBackups(void);
	std::vector<SHistItem> getClientHistory(const std::string& back_start, const std::string& back_stop, const std::string& date_grouping);
	void deleteClientHistoryIds(const std::string& back_start, const std::string& back_stop);
	void deleteClientHistoryItems(const std::string& back_start, const std::string& back_stop);
	void insertClientHistoryId(const std::string& created);
	void insertClientHistoryItem(int id, const std::string& name, const std::string& lastbackup, const std::string& lastseen, const std::string& lastbackup_image, int64 bytes_used_files, int64 bytes_used_images, const std::string& created, int64 hist_id);
	CondInt hasMoreRecentFileBackup(int backupid);
	//@-SQLGenFunctionsEnd

private:
	void createQueries(void);
	void destroyQueries(void);

	IDatabase *db;

	//@-SQLGenVariablesBegin
	IQuery* q_getIncompleteImages;
	IQuery* q_getDeletePendingImages;
	IQuery* q_removeImage;
	IQuery* q_getClientsSortFilebackups;
	IQuery* q_getClientsSortImagebackups;
	IQuery* q_getFullNumImages;
	IQuery* q_getImageRefs;
	IQuery* q_getImageRefsReverse;
	IQuery* q_getImageClientId;
	IQuery* q_getFileBackupClientId;
	IQuery* q_getImageClientname;
	IQuery* q_getImagePath;
	IQuery* q_getIncrNumImages;
	IQuery* q_getIncrNumImagesForBackup;
	IQuery* q_getFullNumFiles;
	IQuery* q_getIncrNumFiles;
	IQuery* q_getClientName;
	IQuery* q_getFileBackupPath;
	IQuery* q_removeFileBackup;
	IQuery* q_getFileBackupInfo;
	IQuery* q_getImageBackupInfo;
	IQuery* q_removeImageSize;
	IQuery* q_addToImageStats;
	IQuery* q_updateDelImageStats;
	IQuery* q_getClientImages;
	IQuery* q_getClientFileBackups;
	IQuery* q_getParentImageBackup;
	IQuery* q_getImageArchived;
	IQuery* q_getAssocImageBackups;
	IQuery* q_getAssocImageBackupsReverse;
	IQuery* q_getImageSize;
	IQuery* q_getClients;
	IQuery* q_getFileBackupsOfClient;
	IQuery* q_getOldImageBackupsOfClient;
	IQuery* q_getImageBackupsOfClient;
	IQuery* q_findFileBackup;
	IQuery* q_getUsedStorage;
	IQuery* q_cleanupBackupLogs;
	IQuery* q_cleanupAuthLog;
	IQuery* q_getIncompleteFileBackups;
	IQuery* q_getDeletePendingFileBackups;
	IQuery* q_getClientHistory;
	IQuery* q_deleteClientHistoryIds;
	IQuery* q_deleteClientHistoryItems;
	IQuery* q_insertClientHistoryId;
	IQuery* q_insertClientHistoryItem;
	IQuery* q_hasMoreRecentFileBackup;
	//@-SQLGenVariablesEnd
};