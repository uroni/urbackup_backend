#include "ServerCleanupDao.h"
#include "../../stringtools.h"

ServerCleanupDao::ServerCleanupDao(IDatabase *db)
	: db(db)
{
	createQueries();
}

ServerCleanupDao::~ServerCleanupDao(void)
{
	destroyQueries();
}

/**
* @-SQLGenAccess
* @func std::vector<SIncompleteImages> ServerCleanupDao::getIncompleteImages
* @return int id, string path
* @sql
*   SELECT id, path
*   FROM backup_images
*   WHERE 
*     complete=0 AND running<datetime('now','-300 seconds')
*/
std::vector<ServerCleanupDao::SIncompleteImages> ServerCleanupDao::getIncompleteImages(void)
{
	if(q_getIncompleteImages==NULL)
	{
		q_getIncompleteImages=db->Prepare("SELECT id, path FROM backup_images WHERE  complete=0 AND running<datetime('now','-300 seconds')", false);
	}
	db_results res=q_getIncompleteImages->Read();
	std::vector<ServerCleanupDao::SIncompleteImages> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeImage
* @sql
*   DELETE FROM backup_images WHERE id=:id(int)
*/
void ServerCleanupDao::removeImage(int id)
{
	if(q_removeImage==NULL)
	{
		q_removeImage=db->Prepare("DELETE FROM backup_images WHERE id=?", false);
	}
	q_removeImage->Bind(id);
	q_removeImage->Write();
	q_removeImage->Reset();
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getClientsSortFilebackups
* @return int id
* @sql
*   SELECT DISTINCT c.id AS id FROM clients c
*		INNER JOIN backups b ON c.id=b.clientid
*	ORDER BY b.backuptime ASC
*/
std::vector<int> ServerCleanupDao::getClientsSortFilebackups(void)
{
	if(q_getClientsSortFilebackups==NULL)
	{
		q_getClientsSortFilebackups=db->Prepare("SELECT DISTINCT c.id AS id FROM clients c INNER JOIN backups b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	}
	db_results res=q_getClientsSortFilebackups->Read();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getClientsSortImagebackups
* @return int id
* @sql
*   SELECT DISTINCT c.id AS id FROM clients c 
*		INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b
*				ON c.id=b.clientid
*	ORDER BY b.backuptime ASC
*/
std::vector<int> ServerCleanupDao::getClientsSortImagebackups(void)
{
	if(q_getClientsSortImagebackups==NULL)
	{
		q_getClientsSortImagebackups=db->Prepare("SELECT DISTINCT c.id AS id FROM clients c  INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	}
	db_results res=q_getClientsSortImagebackups->Read();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDao::getFullNumImages
* @return int id, string letter
* @sql
*   SELECT id, letter FROM backup_images 
*	WHERE clientid=:clientid(int) AND incremental=0 AND complete=1 AND length(letter)<=2
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDao::SImageLetter> ServerCleanupDao::getFullNumImages(int clientid)
{
	if(q_getFullNumImages==NULL)
	{
		q_getFullNumImages=db->Prepare("SELECT id, letter FROM backup_images  WHERE clientid=? AND incremental=0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	}
	q_getFullNumImages->Bind(clientid);
	db_results res=q_getFullNumImages->Read();
	q_getFullNumImages->Reset();
	std::vector<ServerCleanupDao::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].letter=res[i][L"letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageRef> ServerCleanupDao::getImageRefs
* @return int id, int complete
* @sql
*	SELECT id, complete FROM backup_images
*	WHERE incremental<>0 AND incremental_ref=:incremental_ref(int)
*/
std::vector<ServerCleanupDao::SImageRef> ServerCleanupDao::getImageRefs(int incremental_ref)
{
	if(q_getImageRefs==NULL)
	{
		q_getImageRefs=db->Prepare("SELECT id, complete FROM backup_images WHERE incremental<>0 AND incremental_ref=?", false);
	}
	q_getImageRefs->Bind(incremental_ref);
	db_results res=q_getImageRefs->Read();
	q_getImageRefs->Reset();
	std::vector<ServerCleanupDao::SImageRef> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].complete=watoi(res[i][L"complete"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getImagePath
* @return string path
* @sql
*	SELECT path FROM backup_images WHERE id=:id(int)
*/
ServerCleanupDao::CondString ServerCleanupDao::getImagePath(int id)
{
	if(q_getImagePath==NULL)
	{
		q_getImagePath=db->Prepare("SELECT path FROM backup_images WHERE id=?", false);
	}
	q_getImagePath->Bind(id);
	db_results res=q_getImagePath->Read();
	q_getImagePath->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDao::getIncrNumImages
* @return int id, string letter
* @sql
*	SELECT id,letter FROM backup_images
*	WHERE clientid=:clientid(int) AND incremental<>0 AND complete=1 AND length(letter)<=2
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDao::SImageLetter> ServerCleanupDao::getIncrNumImages(int clientid)
{
	if(q_getIncrNumImages==NULL)
	{
		q_getIncrNumImages=db->Prepare("SELECT id,letter FROM backup_images WHERE clientid=? AND incremental<>0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	}
	q_getIncrNumImages->Bind(clientid);
	db_results res=q_getIncrNumImages->Read();
	q_getIncrNumImages->Reset();
	std::vector<ServerCleanupDao::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].letter=res[i][L"letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getFullNumFiles
* @return int id
* @sql
*	SELECT id FROM backups
*	WHERE clientid=:clientid(int) AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0
*   ORDER BY backuptime ASC
*/
std::vector<int> ServerCleanupDao::getFullNumFiles(int clientid)
{
	if(q_getFullNumFiles==NULL)
	{
		q_getFullNumFiles=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	}
	q_getFullNumFiles->Bind(clientid);
	db_results res=q_getFullNumFiles->Read();
	q_getFullNumFiles->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDao::getIncrNumFiles
* @return int id
* @sql
*	SELECT id FROM backups
*	WHERE clientid=:clientid(int) AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0
*	ORDER BY backuptime ASC
*/
std::vector<int> ServerCleanupDao::getIncrNumFiles(int clientid)
{
	if(q_getIncrNumFiles==NULL)
	{
		q_getIncrNumFiles=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	}
	q_getIncrNumFiles->Bind(clientid);
	db_results res=q_getIncrNumFiles->Read();
	q_getIncrNumFiles->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getClientName
* @return string name
* @sql
*	SELECT name FROM clients WHERE id=:clientid(int)
*/
ServerCleanupDao::CondString ServerCleanupDao::getClientName(int clientid)
{
	if(q_getClientName==NULL)
	{
		q_getClientName=db->Prepare("SELECT name FROM clients WHERE id=?", false);
	}
	q_getClientName->Bind(clientid);
	db_results res=q_getClientName->Read();
	q_getClientName->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"name"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getFileBackupPath
* @return string path
* @sql
*	SELECT path FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDao::CondString ServerCleanupDao::getFileBackupPath(int backupid)
{
	if(q_getFileBackupPath==NULL)
	{
		q_getFileBackupPath=db->Prepare("SELECT path FROM backups WHERE id=?", false);
	}
	q_getFileBackupPath->Bind(backupid);
	db_results res=q_getFileBackupPath->Read();
	q_getFileBackupPath->Reset();
	CondString ret = { false, L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::deleteFiles
* @sql
*	DELETE FROM files WHERE backupid=:backupid(int)
*/
void ServerCleanupDao::deleteFiles(int backupid)
{
	if(q_deleteFiles==NULL)
	{
		q_deleteFiles=db->Prepare("DELETE FROM files WHERE backupid=?", false);
	}
	q_deleteFiles->Bind(backupid);
	q_deleteFiles->Write();
	q_deleteFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeFileBackup
* @sql
*	DELETE FROM backups WHERE id=:backupid(int)
*/
void ServerCleanupDao::removeFileBackup(int backupid)
{
	if(q_removeFileBackup==NULL)
	{
		q_removeFileBackup=db->Prepare("DELETE FROM backups WHERE id=?", false);
	}
	q_removeFileBackup->Bind(backupid);
	q_removeFileBackup->Write();
	q_removeFileBackup->Reset();
}

/**
* @-SQLGenAccess
* @func SFileBackupInfo ServerCleanupDao::getFileBackupInfo
* @return int id, string backuptime, string path
* @sql
*	SELECT id, backuptime, path FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDao::SFileBackupInfo ServerCleanupDao::getFileBackupInfo(int backupid)
{
	if(q_getFileBackupInfo==NULL)
	{
		q_getFileBackupInfo=db->Prepare("SELECT id, backuptime, path FROM backups WHERE id=?", false);
	}
	q_getFileBackupInfo->Bind(backupid);
	db_results res=q_getFileBackupInfo->Read();
	q_getFileBackupInfo->Reset();
	SFileBackupInfo ret = { false, 0, L"", L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0][L"id"]);
		ret.backuptime=res[0][L"backuptime"];
		ret.path=res[0][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SImageBackupInfo ServerCleanupDao::getImageBackupInfo
* @return int id, string backuptime, string path, string letter
* @sql
*	SELECT id, backuptime, path, letter FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDao::SImageBackupInfo ServerCleanupDao::getImageBackupInfo(int backupid)
{
	if(q_getImageBackupInfo==NULL)
	{
		q_getImageBackupInfo=db->Prepare("SELECT id, backuptime, path, letter FROM backup_images WHERE id=?", false);
	}
	q_getImageBackupInfo->Bind(backupid);
	db_results res=q_getImageBackupInfo->Read();
	q_getImageBackupInfo->Reset();
	SImageBackupInfo ret = { false, 0, L"", L"", L"" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0][L"id"]);
		ret.backuptime=res[0][L"backuptime"];
		ret.path=res[0][L"path"];
		ret.letter=res[0][L"letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::moveFiles
* @sql
*	INSERT INTO files_del
*		(backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del)
*	SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del
*		FROM files WHERE backupid=:backupid(int)
*/
void ServerCleanupDao::moveFiles(int backupid)
{
	if(q_moveFiles==NULL)
	{
		q_moveFiles=db->Prepare("INSERT INTO files_del (backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del) SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del FROM files WHERE backupid=?", false);
	}
	q_moveFiles->Bind(backupid);
	q_moveFiles->Write();
	q_moveFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeImageSize
* @sql
*	UPDATE clients
*	SET bytes_used_images=( (SELECT bytes_used_images
*						     FROM clients
*						     WHERE id=(
*									    SELECT clientid FROM backup_images
*									    WHERE id=:backupid(int)
*  									  )
*						    )
*						 -  (SELECT size_bytes
*						     FROM backup_images
*						     WHERE id=:backupid(int) )
*						  )
*	WHERE id=(SELECT clientid
*			  FROM backup_images
*			  WHERE id=:backupid(int))
*/
void ServerCleanupDao::removeImageSize(int backupid)
{
	if(q_removeImageSize==NULL)
	{
		q_removeImageSize=db->Prepare("UPDATE clients SET bytes_used_images=( (SELECT bytes_used_images FROM clients WHERE id=( SELECT clientid FROM backup_images WHERE id=? ) ) -  (SELECT size_bytes FROM backup_images WHERE id=? ) ) WHERE id=(SELECT clientid FROM backup_images WHERE id=?)", false);
	}
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Write();
	q_removeImageSize->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::addToImageStats
* @sql
*	INSERT INTO del_stats (backupid, image, delsize, clientid, incremental)
*	SELECT id, 1 AS image, (size_bytes+:size_correction(int64)) AS delsize, clientid, incremental
*		FROM backup_images WHERE id=:backupid(int)
*/
void ServerCleanupDao::addToImageStats(int64 size_correction, int backupid)
{
	if(q_addToImageStats==NULL)
	{
		q_addToImageStats=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental) SELECT id, 1 AS image, (size_bytes+?) AS delsize, clientid, incremental FROM backup_images WHERE id=?", false);
	}
	q_addToImageStats->Bind(size_correction);
	q_addToImageStats->Bind(backupid);
	q_addToImageStats->Write();
	q_addToImageStats->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::updateDelImageStats
* @sql
*	UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=:rowid(int64)
*/
void ServerCleanupDao::updateDelImageStats(int64 rowid)
{
	if(q_updateDelImageStats==NULL)
	{
		q_updateDelImageStats=db->Prepare("UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=?", false);
	}
	q_updateDelImageStats->Bind(rowid);
	q_updateDelImageStats->Write();
	q_updateDelImageStats->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDao::getClientImages
* @return int id, string path
* @sql
*	SELECT id, path FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SImageBackupInfo> ServerCleanupDao::getClientImages(int clientid)
{
	if(q_getClientImages==NULL)
	{
		q_getClientImages=db->Prepare("SELECT id, path FROM backup_images WHERE clientid=?", false);
	}
	q_getClientImages->Bind(clientid);
	db_results res=q_getClientImages->Read();
	q_getClientImages->Reset();
	std::vector<ServerCleanupDao::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerCleanupDao::getClientFileBackups
* @return int id
* @sql
*	SELECT id FROM backups WHERE clientid=:clientid(int)
*/
std::vector<int> ServerCleanupDao::getClientFileBackups(int clientid)
{
	if(q_getClientFileBackups==NULL)
	{
		q_getClientFileBackups=db->Prepare("SELECT id FROM backups WHERE clientid=?", false);
	}
	q_getClientFileBackups->Bind(clientid);
	db_results res=q_getClientFileBackups->Read();
	q_getClientFileBackups->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerCleanupDao::getAssocImageBackups
* @return int assoc_id
* @sql
*	SELECT assoc_id FROM assoc_images WHERE img_id=:img_id(int)
*/
std::vector<int> ServerCleanupDao::getAssocImageBackups(int img_id)
{
	if(q_getAssocImageBackups==NULL)
	{
		q_getAssocImageBackups=db->Prepare("SELECT assoc_id FROM assoc_images WHERE img_id=?", false);
	}
	q_getAssocImageBackups->Bind(img_id);
	db_results res=q_getAssocImageBackups->Read();
	q_getAssocImageBackups->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i][L"assoc_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int64 ServerCleanupDao::getImageSize
* @return int64 size_bytes
* @sql
*	SELECT size_bytes FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDao::CondInt64 ServerCleanupDao::getImageSize(int backupid)
{
	if(q_getImageSize==NULL)
	{
		q_getImageSize=db->Prepare("SELECT size_bytes FROM backup_images WHERE id=?", false);
	}
	q_getImageSize->Bind(backupid);
	db_results res=q_getImageSize->Read();
	q_getImageSize->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"size_bytes"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SClientInfo> ServerCleanupDao::getClients
* @return int id, string name
* @sql
*	SELECT id, name FROM clients
*/
std::vector<ServerCleanupDao::SClientInfo> ServerCleanupDao::getClients(void)
{
	if(q_getClients==NULL)
	{
		q_getClients=db->Prepare("SELECT id, name FROM clients", false);
	}
	db_results res=q_getClients->Read();
	std::vector<ServerCleanupDao::SClientInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].name=res[i][L"name"];
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func vector<SFileBackupInfo> ServerCleanupDao::getFileBackupsOfClient
* @return int id, string backuptime, string path
* @sql
*	SELECT id, backuptime, path FROM backups WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SFileBackupInfo> ServerCleanupDao::getFileBackupsOfClient(int clientid)
{
	if(q_getFileBackupsOfClient==NULL)
	{
		q_getFileBackupsOfClient=db->Prepare("SELECT id, backuptime, path FROM backups WHERE clientid=?", false);
	}
	q_getFileBackupsOfClient->Bind(clientid);
	db_results res=q_getFileBackupsOfClient->Read();
	q_getFileBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SFileBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].backuptime=res[i][L"backuptime"];
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDao::getImageBackupsOfClient
* @return int id, string backuptime, string letter, string path
* @sql
*	SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SImageBackupInfo> ServerCleanupDao::getImageBackupsOfClient(int clientid)
{
	if(q_getImageBackupsOfClient==NULL)
	{
		q_getImageBackupsOfClient=db->Prepare("SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=?", false);
	}
	q_getImageBackupsOfClient->Bind(clientid);
	db_results res=q_getImageBackupsOfClient->Read();
	q_getImageBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].backuptime=res[i][L"backuptime"];
		ret[i].letter=res[i][L"letter"];
		ret[i].path=res[i][L"path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::findFileBackup
* @return int id
* @sql
*	SELECT id FROM backups WHERE clientid=:clientid(int) AND path=:path(string)
*/
ServerCleanupDao::CondInt ServerCleanupDao::findFileBackup(int clientid, const std::wstring& path)
{
	if(q_findFileBackup==NULL)
	{
		q_findFileBackup=db->Prepare("SELECT id FROM backups WHERE clientid=? AND path=?", false);
	}
	q_findFileBackup->Bind(clientid);
	q_findFileBackup->Bind(path);
	db_results res=q_findFileBackup->Read();
	q_findFileBackup->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0][L"id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::removeDanglingFiles
* @sql
*	DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)
*/
void ServerCleanupDao::removeDanglingFiles(void)
{
	if(q_removeDanglingFiles==NULL)
	{
		q_removeDanglingFiles=db->Prepare("DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)", false);
	}
	q_removeDanglingFiles->Write();
}

/**
* @-SQLGenAccess
* @func int64 ServerCleanupDao::getUsedStorage
* @return int64 used_storage
* @sql
*	SELECT (bytes_used_files+bytes_used_images) AS used_storage FROM clients WHERE id=:clientid(int)
*/
ServerCleanupDao::CondInt64 ServerCleanupDao::getUsedStorage(int clientid)
{
	if(q_getUsedStorage==NULL)
	{
		q_getUsedStorage=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used_storage FROM clients WHERE id=?", false);
	}
	q_getUsedStorage->Bind(clientid);
	db_results res=q_getUsedStorage->Read();
	q_getUsedStorage->Reset();
	CondInt64 ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"used_storage"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::cleanupBackupLogs
* @sql
*     DELETE FROM logs WHERE date(created, '+182 days')<date('now')
*/
void ServerCleanupDao::cleanupBackupLogs(void)
{
	if(q_cleanupBackupLogs==NULL)
	{
		q_cleanupBackupLogs=db->Prepare("DELETE FROM logs WHERE date(created, '+182 days')<date('now')", false);
	}
	q_cleanupBackupLogs->Write();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::cleanupAuthLog
* @sql
*     DELETE FROM settings_db.login_access_log WHERE date(logintime, '+182 days')<date('now')
*/
void ServerCleanupDao::cleanupAuthLog(void)
{
	if(q_cleanupAuthLog==NULL)
	{
		q_cleanupAuthLog=db->Prepare("DELETE FROM settings_db.login_access_log WHERE date(logintime, '+182 days')<date('now')", false);
	}
	q_cleanupAuthLog->Write();
}

/**
* @-SQLGenAccess
* @func vector<SIncompleteFileBackup> ServerCleanupDao::getIncompleteFileBackups
* @return int id, int clientid, int incremental, string backuptime, string path, string clientname
* @sql
*      SELECT b.id, b.clientid, b.incremental, b.backuptime, b.path, c.name AS clientname FROM
			backups b INNER JOIN clients c ON b.clientid=c.id
*        WHERE complete=0 AND archived=0 AND EXISTS
*            ( SELECT * FROM backups e WHERE b.clientid = e.clientid AND
*                     e.backuptime>b.backuptime AND e.done=1)
*/
std::vector<ServerCleanupDao::SIncompleteFileBackup> ServerCleanupDao::getIncompleteFileBackups(void)
{
	if(q_getIncompleteFileBackups==NULL)
	{
		q_getIncompleteFileBackups=db->Prepare("SELECT b.id, b.clientid, b.incremental, b.backuptime, b.path, c.name AS clientname FROM backups b INNER JOIN clients c ON b.clientid=c.id WHERE complete=0 AND archived=0 AND EXISTS ( SELECT * FROM backups e WHERE b.clientid = e.clientid AND e.backuptime>b.backuptime AND e.done=1)", false);
	}
	db_results res=q_getIncompleteFileBackups->Read();
	std::vector<ServerCleanupDao::SIncompleteFileBackup> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].clientid=watoi(res[i][L"clientid"]);
		ret[i].incremental=watoi(res[i][L"incremental"]);
		ret[i].backuptime=res[i][L"backuptime"];
		ret[i].path=res[i][L"path"];
		ret[i].clientname=res[i][L"clientname"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SHistItem> ServerCleanupDao::getClientHistory
* @return int id, string name, string lastbackup, string lastseen, string lastbackup_image, int64 bytes_used_files, int64 bytes_used_images, string max_created, int64 hist_id, int current_month, int current_year
* @sql
*    SELECT
*		id, name, MAX(lastbackup) AS lastbackup, MAX(lastseen) AS lastseen, MAX(lastbackup_image) AS lastbackup_image, 
*		MAX(bytes_used_files) AS bytes_used_files, MAX(bytes_used_images) AS bytes_used_images,  MAX(created) AS max_created, MAX(hist_id) AS hist_id
*	 FROM clients_hist
*    WHERE created<=date('now', :back_start(string)) AND created>date('now', :back_stop(string))
*    GROUP BY strftime(:date_grouping(string), created, 'localtime'), id, name
*/
std::vector<ServerCleanupDao::SHistItem> ServerCleanupDao::getClientHistory(const std::wstring& back_start, const std::wstring& back_stop, const std::wstring& date_grouping)
{
	if(q_getClientHistory==NULL)
	{
		q_getClientHistory=db->Prepare("SELECT id, name, MAX(lastbackup) AS lastbackup, MAX(lastseen) AS lastseen, MAX(lastbackup_image) AS lastbackup_image,  MAX(bytes_used_files) AS bytes_used_files, MAX(bytes_used_images) AS bytes_used_images,  MAX(created) AS max_created, MAX(hist_id) AS hist_id FROM clients_hist WHERE created<=date('now', ?) AND created>date('now', ?) GROUP BY strftime(?, created, 'localtime'), id, name", false);
	}
	q_getClientHistory->Bind(back_start);
	q_getClientHistory->Bind(back_stop);
	q_getClientHistory->Bind(date_grouping);
	db_results res=q_getClientHistory->Read();
	q_getClientHistory->Reset();
	std::vector<ServerCleanupDao::SHistItem> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i][L"id"]);
		ret[i].name=res[i][L"name"];
		ret[i].lastbackup=res[i][L"lastbackup"];
		ret[i].lastseen=res[i][L"lastseen"];
		ret[i].lastbackup_image=res[i][L"lastbackup_image"];
		ret[i].bytes_used_files=watoi64(res[i][L"bytes_used_files"]);
		ret[i].bytes_used_images=watoi64(res[i][L"bytes_used_images"]);
		ret[i].max_created=res[i][L"max_created"];
		ret[i].hist_id=watoi64(res[i][L"hist_id"]);
		ret[i].current_month=watoi(res[i][L"current_month"]);
		ret[i].current_year=watoi(res[i][L"current_year"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::deleteClientHistory
* @sql
*    DELETE FROM clients_hist_id WHERE
*				 created<date('now', :back_start(string)) AND created>date('now', :back_stop(string))
*/
void ServerCleanupDao::deleteClientHistory(const std::wstring& back_start, const std::wstring& back_stop)
{
	if(q_deleteClientHistory==NULL)
	{
		q_deleteClientHistory=db->Prepare("DELETE FROM clients_hist_id WHERE created<date('now', ?) AND created>date('now', ?)", false);
	}
	q_deleteClientHistory->Bind(back_start);
	q_deleteClientHistory->Bind(back_stop);
	q_deleteClientHistory->Write();
	q_deleteClientHistory->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::insertClientHistoryId
* @sql
*    INSERT INTO clients_hist_id (created) VALUES (datetime(:created(string)))
*/
void ServerCleanupDao::insertClientHistoryId(const std::wstring& created)
{
	if(q_insertClientHistoryId==NULL)
	{
		q_insertClientHistoryId=db->Prepare("INSERT INTO clients_hist_id (created) VALUES (datetime(?))", false);
	}
	q_insertClientHistoryId->Bind(created);
	q_insertClientHistoryId->Write();
	q_insertClientHistoryId->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::insertClientHistoryItem
* @sql
*    INSERT INTO clients_hist (id, name, lastbackup,
*			lastseen, lastbackup_image, bytes_used_files,
*			bytes_used_images, created, hist_id)
*	 VALUES
*			(:id(int), :name(string), datetime(:lastbackup(string)),
*			datetime(:lastseen(string)), datetime(:lastbackup(lastbackup_image)),
*			:bytes_used_files(int64), :bytes_used_images(int64), 
*			datetime(:lastbackup(created)), :hist_id(int64) )
*/
void ServerCleanupDao::insertClientHistoryItem(int id, const std::wstring& name, const std::wstring& lastbackup, const std::wstring& lastseen, int64 bytes_used_files, int64 bytes_used_images, int64 hist_id)
{
	if(q_insertClientHistoryItem==NULL)
	{
		q_insertClientHistoryItem=db->Prepare("INSERT INTO clients_hist (id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, created, hist_id) VALUES (?, ?, datetime(?), datetime(?), datetime(?), ?, ?,  datetime(?), ? )", false);
	}
	q_insertClientHistoryItem->Bind(id);
	q_insertClientHistoryItem->Bind(name);
	q_insertClientHistoryItem->Bind(lastbackup);
	q_insertClientHistoryItem->Bind(lastseen);
	q_insertClientHistoryItem->Bind(lastbackup);
	q_insertClientHistoryItem->Bind(bytes_used_files);
	q_insertClientHistoryItem->Bind(bytes_used_images);
	q_insertClientHistoryItem->Bind(lastbackup);
	q_insertClientHistoryItem->Bind(hist_id);
	q_insertClientHistoryItem->Write();
	q_insertClientHistoryItem->Reset();
}


//@-SQLGenSetup
void ServerCleanupDao::createQueries(void)
{
	q_getIncompleteImages=NULL;
	q_removeImage=NULL;
	q_getClientsSortFilebackups=NULL;
	q_getClientsSortImagebackups=NULL;
	q_getFullNumImages=NULL;
	q_getImageRefs=NULL;
	q_getImagePath=NULL;
	q_getIncrNumImages=NULL;
	q_getFullNumFiles=NULL;
	q_getIncrNumFiles=NULL;
	q_getClientName=NULL;
	q_getFileBackupPath=NULL;
	q_deleteFiles=NULL;
	q_removeFileBackup=NULL;
	q_getFileBackupInfo=NULL;
	q_getImageBackupInfo=NULL;
	q_moveFiles=NULL;
	q_removeImageSize=NULL;
	q_addToImageStats=NULL;
	q_updateDelImageStats=NULL;
	q_getClientImages=NULL;
	q_getClientFileBackups=NULL;
	q_getAssocImageBackups=NULL;
	q_getImageSize=NULL;
	q_getClients=NULL;
	q_getFileBackupsOfClient=NULL;
	q_getImageBackupsOfClient=NULL;
	q_findFileBackup=NULL;
	q_removeDanglingFiles=NULL;
	q_getUsedStorage=NULL;
	q_cleanupBackupLogs=NULL;
	q_cleanupAuthLog=NULL;
	q_getIncompleteFileBackups=NULL;
	q_getClientHistory=NULL;
	q_deleteClientHistory=NULL;
	q_insertClientHistoryId=NULL;
	q_insertClientHistoryItem=NULL;
}

//@-SQLGenDestruction
void ServerCleanupDao::destroyQueries(void)
{
	db->destroyQuery(q_getIncompleteImages);
	db->destroyQuery(q_removeImage);
	db->destroyQuery(q_getClientsSortFilebackups);
	db->destroyQuery(q_getClientsSortImagebackups);
	db->destroyQuery(q_getFullNumImages);
	db->destroyQuery(q_getImageRefs);
	db->destroyQuery(q_getImagePath);
	db->destroyQuery(q_getIncrNumImages);
	db->destroyQuery(q_getFullNumFiles);
	db->destroyQuery(q_getIncrNumFiles);
	db->destroyQuery(q_getClientName);
	db->destroyQuery(q_getFileBackupPath);
	db->destroyQuery(q_deleteFiles);
	db->destroyQuery(q_removeFileBackup);
	db->destroyQuery(q_getFileBackupInfo);
	db->destroyQuery(q_getImageBackupInfo);
	db->destroyQuery(q_moveFiles);
	db->destroyQuery(q_removeImageSize);
	db->destroyQuery(q_addToImageStats);
	db->destroyQuery(q_updateDelImageStats);
	db->destroyQuery(q_getClientImages);
	db->destroyQuery(q_getClientFileBackups);
	db->destroyQuery(q_getAssocImageBackups);
	db->destroyQuery(q_getImageSize);
	db->destroyQuery(q_getClients);
	db->destroyQuery(q_getFileBackupsOfClient);
	db->destroyQuery(q_getImageBackupsOfClient);
	db->destroyQuery(q_findFileBackup);
	db->destroyQuery(q_removeDanglingFiles);
	db->destroyQuery(q_getUsedStorage);
	db->destroyQuery(q_cleanupBackupLogs);
	db->destroyQuery(q_cleanupAuthLog);
	db->destroyQuery(q_getIncompleteFileBackups);
	db->destroyQuery(q_getClientHistory);
	db->destroyQuery(q_deleteClientHistory);
	db->destroyQuery(q_insertClientHistoryId);
	db->destroyQuery(q_insertClientHistoryItem);
}