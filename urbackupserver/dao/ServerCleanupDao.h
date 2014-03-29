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
	std::vector<int> getFullNumFiles(int clientid);
	std::vector<int> getIncrNumFiles(int clientid);
	CondString getClientName(int clientid);
	CondString getFileBackupPath(int backupid);
	void deleteFiles(int backupid);
	void removeFileBackup(int backupid);
	SFileBackupInfo getFileBackupInfo(int backupid);
	SImageBackupInfo getImageBackupInfo(int backupid);
	void moveFiles(int backupid);
	void removeImageSize(int backupid);
	void addToImageStats(int64 size_correction, int backupid);
	void updateDelImageStats(int64 rowid);
	std::vector<SImageBackupInfo> getClientImages(int clientid);
	std::vector<int> getClientFileBackups(int clientid);
	std::vector<int> getAssocImageBackups(int img_id);
	CondInt64 getImageSize(int backupid);
	std::vector<SClientInfo> getClients(void);
	std::vector<SFileBackupInfo> getFileBackupsOfClient(int clientid);
	std::vector<SImageBackupInfo> getImageBackupsOfClient(int clientid);
	CondInt findFileBackup(int clientid, const std::wstring& path);
	void removeDanglingFiles(void);
	CondInt64 getUsedStorage(int clientid);
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
	IQuery* q_getFullNumFiles;
	IQuery* q_getIncrNumFiles;
	IQuery* q_getClientName;
	IQuery* q_getFileBackupPath;
	IQuery* q_deleteFiles;
	IQuery* q_removeFileBackup;
	IQuery* q_getFileBackupInfo;
	IQuery* q_getImageBackupInfo;
	IQuery* q_moveFiles;
	IQuery* q_removeImageSize;
	IQuery* q_addToImageStats;
	IQuery* q_updateDelImageStats;
	IQuery* q_getClientImages;
	IQuery* q_getClientFileBackups;
	IQuery* q_getAssocImageBackups;
	IQuery* q_getImageSize;
	IQuery* q_getClients;
	IQuery* q_getFileBackupsOfClient;
	IQuery* q_getImageBackupsOfClient;
	IQuery* q_findFileBackup;
	IQuery* q_removeDanglingFiles;
	IQuery* q_getUsedStorage;
	//@-SQLGenVariablesEnd
};