#include "ServerCleanupDAO.h"
#include "../../stringtools.h"

ServerCleanupDAO::ServerCleanupDAO(IDatabase *db)
	: db(db)
{
	createQueries();
}

ServerCleanupDAO::~ServerCleanupDAO(void)
{
	destroyQueries();
}

/**
* @-SQLGenAccess
* @func std::vector<SIncompleteImages> ServerCleanupDAO::getIncompleteImages
* @return int id, string path
* @sql
*   SELECT id, path
*   FROM backup_images
*   WHERE 
*     complete=0 AND running<datetime('now','-300 seconds')
*/
std::vector<ServerCleanupDAO::SIncompleteImages> ServerCleanupDAO::getIncompleteImages(void)
{
	db_results res=q_getIncompleteImages->Read();
	std::vector<ServerCleanupDAO::SIncompleteImages> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SIncompleteImages tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.path=res[i][L"path"];
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::removeImage
* @sql
*   DELETE FROM backup_images WHERE id=:id(int)
*/
void ServerCleanupDAO::removeImage(int id)
{
	q_removeImage->Bind(id);
	q_removeImage->Write();
	q_removeImage->Reset();
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDAO::getClientsSortFilebackups
* @return int id
* @sql
*   SELECT DISTINCT c.id AS id FROM clients c
*		INNER JOIN backups b ON c.id=b.clientid
*	ORDER BY b.backuptime ASC
*/
std::vector<int> ServerCleanupDAO::getClientsSortFilebackups(void)
{
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
* @func std::vector<int> ServerCleanupDAO::getClientsSortImagebackups
* @return int id
* @sql
*   SELECT DISTINCT c.id FROM clients c 
*		INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b
*				ON c.id=b.clientid
*	ORDER BY b.backuptime ASC
*/
std::vector<int> ServerCleanupDAO::getClientsSortImagebackups(void)
{
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
* @func std::vector<SImageLetter> ServerCleanupDAO::getFullNumImages
* @return int id, string letter
* @sql
*   SELECT id, letter FROM backup_images 
*	WHERE clientid=:clientid(int) AND incremental=0 AND complete=1 AND length(letter)<=2
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDAO::SImageLetter> ServerCleanupDAO::getFullNumImages(int clientid)
{
	q_getFullNumImages->Bind(clientid);
	db_results res=q_getFullNumImages->Read();
	q_getFullNumImages->Reset();
	std::vector<ServerCleanupDAO::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SImageLetter tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.letter=res[i][L"letter"];
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageRef> ServerCleanupDAO::getImageRefs
* @return int id, int complete
* @sql
*	SELECT id, complete FROM backup_images
*	WHERE incremental<>0 AND incremental_ref=:incremental_ref(int)
*/
std::vector<ServerCleanupDAO::SImageRef> ServerCleanupDAO::getImageRefs(int incremental_ref)
{
	q_getImageRefs->Bind(incremental_ref);
	db_results res=q_getImageRefs->Read();
	q_getImageRefs->Reset();
	std::vector<ServerCleanupDAO::SImageRef> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SImageRef tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.complete=watoi(res[i][L"complete"]);
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDAO::getImagePath
* @return string path
* @sql
*	SELECT path FROM backup_images WHERE id=:id(int)
*/
ServerCleanupDAO::CondString ServerCleanupDAO::getImagePath(int id)
{
	q_getImagePath->Bind(id);
	db_results res=q_getImagePath->Read();
	q_getImagePath->Reset();
	CondString ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"path"];
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDAO::getIncrNumImages
* @return int id, string letter
* @sql
*	SELECT id,letter FROM backup_images
*	WHERE clientid=:clientid(int) AND incremental<>0 AND complete=1 AND length(letter)<=2
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDAO::SImageLetter> ServerCleanupDAO::getIncrNumImages(int clientid)
{
	q_getIncrNumImages->Bind(clientid);
	db_results res=q_getIncrNumImages->Read();
	q_getIncrNumImages->Reset();
	std::vector<ServerCleanupDAO::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SImageLetter tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.letter=res[i][L"letter"];
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<int> ServerCleanupDAO::getFullNumFiles
* @return int id
* @sql
*	SELECT id FROM backups
*	WHERE clientid=:clientid(int) AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0
*   ORDER BY backuptime ASC
*/
std::vector<int> ServerCleanupDAO::getFullNumFiles(int clientid)
{
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
* @func std::vector<int> ServerCleanupDAO::getIncrNumFiles
* @return int id
* @sql
*	SELECT id FROM backups
*	WHERE clientid=:clientid(int) AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0
*	ORDER BY backuptime ASC
*/
std::vector<int> ServerCleanupDAO::getIncrNumFiles(int clientid)
{
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
* @func string ServerCleanupDAO::getClientName
* @return string name
* @sql
*	SELECT name FROM clients WHERE id=:clientid(int)
*/
ServerCleanupDAO::CondString ServerCleanupDAO::getClientName(int clientid)
{
	q_getClientName->Bind(clientid);
	db_results res=q_getClientName->Read();
	q_getClientName->Reset();
	CondString ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"name"];
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDAO::getFileBackupPath
* @return string path
* @sql
*	SELECT path FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDAO::CondString ServerCleanupDAO::getFileBackupPath(int backupid)
{
	q_getFileBackupPath->Bind(backupid);
	db_results res=q_getFileBackupPath->Read();
	q_getFileBackupPath->Reset();
	CondString ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0][L"path"];
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::deleteFiles
* @sql
*	DELETE FROM files WHERE backupid=:backupid(int)
*/
void ServerCleanupDAO::deleteFiles(int backupid)
{
	q_deleteFiles->Bind(backupid);
	q_deleteFiles->Write();
	q_deleteFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::removeFileBackup
* @sql
*	DELETE FROM backups WHERE id=:backupid(int)
*/
void ServerCleanupDAO::removeFileBackup(int backupid)
{
	q_removeFileBackup->Bind(backupid);
	q_removeFileBackup->Write();
	q_removeFileBackup->Reset();
}

/**
* @-SQLGenAccess
* @func SFileBackupInfo ServerCleanupDAO::getFileBackupInfo
* @return int id, string backuptime, string path
* @sql
*	SELECT id, backuptime, path FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDAO::SFileBackupInfo ServerCleanupDAO::getFileBackupInfo(int backupid)
{
	q_getFileBackupInfo->Bind(backupid);
	db_results res=q_getFileBackupInfo->Read();
	q_getFileBackupInfo->Reset();
	SFileBackupInfo ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0][L"id"]);
		ret.backuptime=res[0][L"backuptime"];
		ret.path=res[0][L"path"];
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SImageBackupInfo ServerCleanupDAO::getImageBackupInfo
* @return int id, string backuptime, string path, string letter
* @sql
*	SELECT id, backuptime, path, letter FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDAO::SImageBackupInfo ServerCleanupDAO::getImageBackupInfo(int backupid)
{
	q_getImageBackupInfo->Bind(backupid);
	db_results res=q_getImageBackupInfo->Read();
	q_getImageBackupInfo->Reset();
	SImageBackupInfo ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0][L"id"]);
		ret.backuptime=res[0][L"backuptime"];
		ret.path=res[0][L"path"];
		ret.letter=res[0][L"letter"];
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::moveFiles
* @sql
*	INSERT INTO files_del
*		(backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del)
*	SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del
*		FROM files WHERE backupid=:backupid(int)
*/
void ServerCleanupDAO::moveFiles(int backupid)
{
	q_moveFiles->Bind(backupid);
	db_results res=q_moveFiles->Read();
	q_moveFiles->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::removeImageSize
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
void ServerCleanupDAO::removeImageSize(int backupid)
{
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Bind(backupid);
	q_removeImageSize->Bind(backupid);
	db_results res=q_removeImageSize->Read();
	q_removeImageSize->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::addToImageStats
* @sql
*	INSERT INTO del_stats (backupid, image, delsize, clientid, incremental)
*	SELECT id, 1 AS image, (size_bytes+:size_correction(int64)) AS delsize, clientid, incremental
*		FROM backup_images WHERE id=:backupid(int)
*/
void ServerCleanupDAO::addToImageStats(int64 size_correction, int backupid)
{
	q_addToImageStats->Bind(size_correction);
	q_addToImageStats->Bind(backupid);
	db_results res=q_addToImageStats->Read();
	q_addToImageStats->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::updateDelImageStats
* @sql
*	UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=:rowid(int64)
*/
void ServerCleanupDAO::updateDelImageStats(int64 rowid)
{
	q_updateDelImageStats->Bind(rowid);
	q_updateDelImageStats->Reset();
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDAO::getClientImages
* @return int id, string path
* @sql
*	SELECT id, path FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDAO::SImageBackupInfo> ServerCleanupDAO::getClientImages(int clientid)
{
	q_getClientImages->Bind(clientid);
	db_results res=q_getClientImages->Read();
	q_getClientImages->Reset();
	std::vector<ServerCleanupDAO::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SImageBackupInfo tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.path=res[i][L"path"];
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerCleanupDAO::getClientFileBackups
* @return int id
* @sql
*	SELECT id FROM backups WHERE clientid=:clientid(int)
*/
std::vector<int> ServerCleanupDAO::getClientFileBackups(int clientid)
{
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
* @func vector<int> ServerCleanupDAO::getAssocImageBackups
* @return int assoc_id
* @sql
*	SELECT assoc_id FROM assoc_images WHERE img_id=:img_id(int)
*/
std::vector<int> ServerCleanupDAO::getAssocImageBackups(int img_id)
{
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
* @func int64 ServerCleanupDAO::getImageSize
* @return int64 size_bytes
* @sql
*	SELECT size_bytes FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDAO::CondInt64 ServerCleanupDAO::getImageSize(int backupid)
{
	q_getImageSize->Bind(backupid);
	db_results res=q_getImageSize->Read();
	q_getImageSize->Reset();
	CondInt64 ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"size_bytes"]);
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SClientInfo> ServerCleanupDAO::getClients
* @return int id, string name
* @sql
*	SELECT id, name FROM clients
*/
std::vector<ServerCleanupDAO::SClientInfo> ServerCleanupDAO::getClients(void)
{
	db_results res=q_getClients->Read();
	std::vector<ServerCleanupDAO::SClientInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SClientInfo tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.name=res[i][L"name"];
		ret[i]=tmp;
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func vector<SFileBackupInfo> ServerCleanupDAO::getFileBackupsOfClient
* @return int id, string backuptime, string path
* @sql
*	SELECT id, backuptime, path FROM backups WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDAO::SFileBackupInfo> ServerCleanupDAO::getFileBackupsOfClient(int clientid)
{
	q_getFileBackupsOfClient->Bind(clientid);
	db_results res=q_getFileBackupsOfClient->Read();
	q_getFileBackupsOfClient->Reset();
	std::vector<ServerCleanupDAO::SFileBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SFileBackupInfo tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.backuptime=res[i][L"backuptime"];
		tmp.path=res[i][L"path"];
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDAO::getImageBackupsOfClient
* @return int id, string backuptime, string letter, string path
* @sql
*	SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDAO::SImageBackupInfo> ServerCleanupDAO::getImageBackupsOfClient(int clientid)
{
	q_getImageBackupsOfClient->Bind(clientid);
	db_results res=q_getImageBackupsOfClient->Read();
	q_getImageBackupsOfClient->Reset();
	std::vector<ServerCleanupDAO::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		SImageBackupInfo tmp;
		tmp.id=watoi(res[i][L"id"]);
		tmp.backuptime=res[i][L"backuptime"];
		tmp.letter=res[i][L"letter"];
		tmp.path=res[i][L"path"];
		ret[i]=tmp;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDAO::findFileBackup
* @return int id
* @sql
*	SELECT id FROM backups WHERE clientid=:clientid(int) AND path=:path(string)
*/
ServerCleanupDAO::CondInt ServerCleanupDAO::findFileBackup(int clientid, const std::wstring& path)
{
	q_findFileBackup->Bind(clientid);
	q_findFileBackup->Bind(path);
	db_results res=q_findFileBackup->Read();
	q_findFileBackup->Reset();
	CondInt ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0][L"id"]);
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDAO::removeDanglingFiles
* @sql
*	DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)
*/
void ServerCleanupDAO::removeDanglingFiles(void)
{
	q_removeDanglingFiles->Write();
}

/**
* @-SQLGenAccess
* @func int64 ServerCleanupDAO::getUsedStorage
* @return int64 used_storage
* @sql
*	SELECT (bytes_used_files+bytes_used_images) AS used_storage FROM clients WHERE id=:clientid(int)
*/
ServerCleanupDAO::CondInt64 ServerCleanupDAO::getUsedStorage(int clientid)
{
	q_getUsedStorage->Bind(clientid);
	db_results res=q_getUsedStorage->Read();
	q_getUsedStorage->Reset();
	CondInt64 ret;
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi64(res[0][L"used_storage"]);
	}
	else
	{
		ret.exists=false;
	}
	return ret;
}



//@-SQLGenSetup
void ServerCleanupDAO::createQueries(void)
{
	q_getIncompleteImages=db->Prepare("SELECT id, path FROM backup_images WHERE  complete=0 AND running<datetime('now','-300 seconds')", false);
	q_removeImage=db->Prepare("DELETE FROM backup_images WHERE id=?", false);
	q_getClientsSortFilebackups=db->Prepare("SELECT DISTINCT c.id AS id FROM clients c INNER JOIN backups b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_getClientsSortImagebackups=db->Prepare("SELECT DISTINCT c.id FROM clients c  INNER JOIN (SELECT * FROM backup_images WHERE length(letter)<=2) b ON c.id=b.clientid ORDER BY b.backuptime ASC", false);
	q_getFullNumImages=db->Prepare("SELECT id, letter FROM backup_images  WHERE clientid=? AND incremental=0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	q_getImageRefs=db->Prepare("SELECT id, complete FROM backup_images WHERE incremental<>0 AND incremental_ref=?", false);
	q_getImagePath=db->Prepare("SELECT path FROM backup_images WHERE id=?", false);
	q_getIncrNumImages=db->Prepare("SELECT id,letter FROM backup_images WHERE clientid=? AND incremental<>0 AND complete=1 AND length(letter)<=2 ORDER BY backuptime ASC", false);
	q_getFullNumFiles=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental=0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	q_getIncrNumFiles=db->Prepare("SELECT id FROM backups WHERE clientid=? AND incremental<>0 AND running<datetime('now','-300 seconds') AND archived=0 ORDER BY backuptime ASC", false);
	q_getClientName=db->Prepare("SELECT name FROM clients WHERE id=?", false);
	q_getFileBackupPath=db->Prepare("SELECT path FROM backups WHERE id=?", false);
	q_deleteFiles=db->Prepare("DELETE FROM files WHERE backupid=?", false);
	q_removeFileBackup=db->Prepare("DELETE FROM backups WHERE id=?", false);
	q_getFileBackupInfo=db->Prepare("SELECT id, backuptime, path FROM backups WHERE id=?", false);
	q_getImageBackupInfo=db->Prepare("SELECT id, backuptime, path, letter FROM backup_images WHERE id=?", false);
	q_moveFiles=db->Prepare("INSERT INTO files_del (backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, is_del) SELECT backupid, fullpath, shahash, filesize, created, rsize, clientid, incremental, 1 AS is_del FROM files WHERE backupid=?", false);
	q_removeImageSize=db->Prepare("UPDATE clients SET bytes_used_images=( (SELECT bytes_used_images FROM clients WHERE id=( SELECT clientid FROM backup_images WHERE id=? ) ) -  (SELECT size_bytes FROM backup_images WHERE id=? ) ) WHERE id=(SELECT clientid FROM backup_images WHERE id=?)", false);
	q_addToImageStats=db->Prepare("INSERT INTO del_stats (backupid, image, delsize, clientid, incremental) SELECT id, 1 AS image, (size_bytes+?) AS delsize, clientid, incremental FROM backup_images WHERE id=?", false);
	q_updateDelImageStats=db->Prepare("UPDATE del_stats SET stoptime=CURRENT_TIMESTAMP WHERE rowid=?", false);
	q_getClientImages=db->Prepare("SELECT id, path FROM backup_images WHERE clientid=?", false);
	q_getClientFileBackups=db->Prepare("SELECT id FROM backups WHERE clientid=?", false);
	q_getAssocImageBackups=db->Prepare("SELECT assoc_id FROM assoc_images WHERE img_id=?", false);
	q_getImageSize=db->Prepare("SELECT size_bytes FROM backup_images WHERE id=?", false);
	q_getClients=db->Prepare("SELECT id, name FROM clients", false);
	q_getFileBackupsOfClient=db->Prepare("SELECT id, backuptime, path FROM backups WHERE clientid=?", false);
	q_getImageBackupsOfClient=db->Prepare("SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=?", false);
	q_findFileBackup=db->Prepare("SELECT id FROM backups WHERE clientid=? AND path=?", false);
	q_removeDanglingFiles=db->Prepare("DELETE FROM files WHERE backupid NOT IN (SELECT id FROM backups)", false);
	q_getUsedStorage=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used_storage FROM clients WHERE id=?", false);
}

//@-SQLGenDestruction
void ServerCleanupDAO::destroyQueries(void)
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
}