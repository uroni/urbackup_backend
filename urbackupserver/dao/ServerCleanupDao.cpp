/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "ServerCleanupDao.h"
#include "../../stringtools.h"
#include <assert.h>

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
* @return int id, string path, string clientname
* @sql
*   SELECT b.id AS id, b.path AS path, c.name AS clientname
*   FROM backup_images b, clients c
*   WHERE 
*     complete=0 AND archived=0 AND running<datetime('now','-300 seconds')
*	  AND b.clientid=c.id
*/
std::vector<ServerCleanupDao::SIncompleteImages> ServerCleanupDao::getIncompleteImages(void)
{
	if(q_getIncompleteImages==NULL)
	{
		q_getIncompleteImages=db->Prepare("SELECT b.id AS id, b.path AS path, c.name AS clientname FROM backup_images b, clients c WHERE  complete=0 AND archived=0 AND running<datetime('now','-300 seconds') AND b.clientid=c.id", false);
	}
	db_results res=q_getIncompleteImages->Read();
	std::vector<ServerCleanupDao::SIncompleteImages> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].path=res[i]["path"];
		ret[i].clientname=res[i]["clientname"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::getIncompleteImage
* @return int id
* @sql
*   SELECT id
*   FROM backup_images
*   WHERE
*     complete=0 AND (archived & 1)=0 AND running<datetime('now','-300 seconds')
*	  AND id=:id(int)
*/
ServerCleanupDao::CondInt ServerCleanupDao::getIncompleteImage(int id)
{
	if(q_getIncompleteImage==NULL)
	{
		q_getIncompleteImage=db->Prepare("SELECT id FROM backup_images WHERE complete=0 AND (archived & 1)=0 AND running<datetime('now','-300 seconds') AND id=?", false);
	}
	q_getIncompleteImage->Bind(id);
	db_results res=q_getIncompleteImage->Read();
	q_getIncompleteImage->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SIncompleteImages> ServerCleanupDao::getDeletePendingImages
* @return int id, string path, string clientname
* @sql
*   SELECT b.id AS id, b.path AS path, c.name AS clientname
*   FROM backup_images b, clients c
*   WHERE
*     b.delete_pending=1 AND b.clientid=c.id
*/
std::vector<ServerCleanupDao::SIncompleteImages> ServerCleanupDao::getDeletePendingImages(void)
{
	if(q_getDeletePendingImages==NULL)
	{
		q_getDeletePendingImages=db->Prepare("SELECT b.id AS id, b.path AS path, c.name AS clientname FROM backup_images b, clients c WHERE b.delete_pending=1 AND b.clientid=c.id", false);
	}
	db_results res=q_getDeletePendingImages->Read();
	std::vector<ServerCleanupDao::SIncompleteImages> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].path=res[i]["path"];
		ret[i].clientname=res[i]["clientname"];
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
		ret[i]=watoi(res[i]["id"]);
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
		ret[i]=watoi(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDao::getFullNumImages
* @return int id, string letter
* @sql
*   SELECT id, letter FROM backup_images 
*	WHERE clientid=:clientid(int) AND incremental=0 AND complete=1 AND length(letter)<=2 AND archived=0
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDao::SImageLetter> ServerCleanupDao::getFullNumImages(int clientid)
{
	if(q_getFullNumImages==NULL)
	{
		q_getFullNumImages=db->Prepare("SELECT id, letter FROM backup_images  WHERE clientid=? AND incremental=0 AND complete=1 AND length(letter)<=2 AND archived=0 ORDER BY backuptime ASC", false);
	}
	q_getFullNumImages->Bind(clientid);
	db_results res=q_getFullNumImages->Read();
	q_getFullNumImages->Reset();
	std::vector<ServerCleanupDao::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].letter=res[i]["letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageRef> ServerCleanupDao::getImageRefs
* @return int id, int complete, int archived
* @sql
*	SELECT id, complete, archived FROM backup_images
*	WHERE incremental<>0 AND incremental_ref=:incremental_ref(int)
*/
std::vector<ServerCleanupDao::SImageRef> ServerCleanupDao::getImageRefs(int incremental_ref)
{
	if(q_getImageRefs==NULL)
	{
		q_getImageRefs=db->Prepare("SELECT id, complete, archived FROM backup_images WHERE incremental<>0 AND incremental_ref=?", false);
	}
	q_getImageRefs->Bind(incremental_ref);
	db_results res=q_getImageRefs->Read();
	q_getImageRefs->Reset();
	std::vector<ServerCleanupDao::SImageRef> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].complete=watoi(res[i]["complete"]);
		ret[i].archived=watoi(res[i]["archived"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageRef> ServerCleanupDao::getImageRefsReverse
* @return int id, int complete, int archived
* @sql
*	SELECT id, complete, archived FROM backup_images
*	WHERE id = (SELECT incremental_ref FROM backup_images WHERE id=:backupid(int))
*/
std::vector<ServerCleanupDao::SImageRef> ServerCleanupDao::getImageRefsReverse(int backupid)
{
	if(q_getImageRefsReverse==NULL)
	{
		q_getImageRefsReverse=db->Prepare("SELECT id, complete, archived FROM backup_images WHERE id = (SELECT incremental_ref FROM backup_images WHERE id=?)", false);
	}
	q_getImageRefsReverse->Bind(backupid);
	db_results res=q_getImageRefsReverse->Read();
	q_getImageRefsReverse->Reset();
	std::vector<ServerCleanupDao::SImageRef> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].complete=watoi(res[i]["complete"]);
		ret[i].archived=watoi(res[i]["archived"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getImageClientId
* @return int clientid
* @sql
*	SELECT clientid FROM backup_images WHERE id=:id(int)
*/
ServerCleanupDao::CondInt ServerCleanupDao::getImageClientId(int id)
{
	if(q_getImageClientId==NULL)
	{
		q_getImageClientId=db->Prepare("SELECT clientid FROM backup_images WHERE id=?", false);
	}
	q_getImageClientId->Bind(id);
	db_results res=q_getImageClientId->Read();
	q_getImageClientId->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["clientid"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getFileBackupClientId
* @return int clientid
* @sql
*	SELECT clientid FROM backups WHERE id=:id(int)
*/
ServerCleanupDao::CondInt ServerCleanupDao::getFileBackupClientId(int id)
{
	if(q_getFileBackupClientId==NULL)
	{
		q_getFileBackupClientId=db->Prepare("SELECT clientid FROM backups WHERE id=?", false);
	}
	q_getFileBackupClientId->Bind(id);
	db_results res=q_getFileBackupClientId->Read();
	q_getFileBackupClientId->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["clientid"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func string ServerCleanupDao::getImageClientname
* @return string name
* @sql
*	SELECT name FROM clients WHERE id=(SELECT clientid FROM backup_images WHERE id=:id(int) )
*/
ServerCleanupDao::CondString ServerCleanupDao::getImageClientname(int id)
{
	if(q_getImageClientname==NULL)
	{
		q_getImageClientname=db->Prepare("SELECT name FROM clients WHERE id=(SELECT clientid FROM backup_images WHERE id=? )", false);
	}
	q_getImageClientname->Bind(id);
	db_results res=q_getImageClientname->Read();
	q_getImageClientname->Reset();
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["name"];
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
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func std::vector<SImageLetter> ServerCleanupDao::getIncrNumImages
* @return int id, string letter
* @sql
*	SELECT id,letter FROM backup_images
*	WHERE clientid=:clientid(int) AND incremental<>0 AND complete=1 AND length(letter)<=2 AND archived=0
*	ORDER BY backuptime ASC
*/
std::vector<ServerCleanupDao::SImageLetter> ServerCleanupDao::getIncrNumImages(int clientid)
{
	if(q_getIncrNumImages==NULL)
	{
		q_getIncrNumImages=db->Prepare("SELECT id,letter FROM backup_images WHERE clientid=? AND incremental<>0 AND complete=1 AND length(letter)<=2 AND archived=0 ORDER BY backuptime ASC", false);
	}
	q_getIncrNumImages->Bind(clientid);
	db_results res=q_getIncrNumImages->Read();
	q_getIncrNumImages->Reset();
	std::vector<ServerCleanupDao::SImageLetter> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].letter=res[i]["letter"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::getIncrNumImagesForBackup
* @return int_raw c
* @sql
*	SELECT COUNT(id) AS c FROM backup_images
*	WHERE clientid=(SELECT clientid FROM backup_images WHERE id=:backupid(int))
*			AND incremental<>0 AND complete=1 AND letter=(SELECT letter FROM backup_images WHERE id=:backupid(int)) AND archived=0
*/
int ServerCleanupDao::getIncrNumImagesForBackup(int backupid)
{
	if(q_getIncrNumImagesForBackup==NULL)
	{
		q_getIncrNumImagesForBackup=db->Prepare("SELECT COUNT(id) AS c FROM backup_images WHERE clientid=(SELECT clientid FROM backup_images WHERE id=?) AND incremental<>0 AND complete=1 AND letter=(SELECT letter FROM backup_images WHERE id=?) AND archived=0", false);
	}
	q_getIncrNumImagesForBackup->Bind(backupid);
	q_getIncrNumImagesForBackup->Bind(backupid);
	db_results res=q_getIncrNumImagesForBackup->Read();
	q_getIncrNumImagesForBackup->Reset();
	assert(!res.empty());
	return watoi(res[0]["c"]);
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
		ret[i]=watoi(res[i]["id"]);
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
		ret[i]=watoi(res[i]["id"]);
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
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["name"];
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
	CondString ret = { false, "" };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=res[0]["path"];
	}
	return ret;
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
* @return int id, string backuptime, string path, int done
* @sql
*	SELECT id, backuptime, path, done FROM backups WHERE id=:backupid(int)
*/
ServerCleanupDao::SFileBackupInfo ServerCleanupDao::getFileBackupInfo(int backupid)
{
	if(q_getFileBackupInfo==NULL)
	{
		q_getFileBackupInfo=db->Prepare("SELECT id, backuptime, path, done FROM backups WHERE id=?", false);
	}
	q_getFileBackupInfo->Bind(backupid);
	db_results res=q_getFileBackupInfo->Read();
	q_getFileBackupInfo->Reset();
	SFileBackupInfo ret = { false, 0, "", "", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0]["id"]);
		ret.backuptime=res[0]["backuptime"];
		ret.path=res[0]["path"];
		ret.done=watoi(res[0]["done"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func SImageBackupInfo ServerCleanupDao::getImageBackupInfo
* @return int id, string backuptime, string path, string letter, int complete
* @sql
*	SELECT id, backuptime, path, letter, complete FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDao::SImageBackupInfo ServerCleanupDao::getImageBackupInfo(int backupid)
{
	if(q_getImageBackupInfo==NULL)
	{
		q_getImageBackupInfo=db->Prepare("SELECT id, backuptime, path, letter, complete FROM backup_images WHERE id=?", false);
	}
	q_getImageBackupInfo->Bind(backupid);
	db_results res=q_getImageBackupInfo->Read();
	q_getImageBackupInfo->Reset();
	SImageBackupInfo ret = { false, 0, "", "", "", 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.id=watoi(res[0]["id"]);
		ret.backuptime=res[0]["backuptime"];
		ret.path=res[0]["path"];
		ret.letter=res[0]["letter"];
		ret.complete=watoi(res[0]["complete"]);
	}
	return ret;
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
		ret[i].id=watoi(res[i]["id"]);
		ret[i].path=res[i]["path"];
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
		ret[i]=watoi(res[i]["id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::getParentImageBackup
* @return int img_id
* @sql
*	SELECT img_id FROM assoc_images WHERE assoc_id=:assoc_id(int)
*/
ServerCleanupDao::CondInt ServerCleanupDao::getParentImageBackup(int assoc_id)
{
	if(q_getParentImageBackup==NULL)
	{
		q_getParentImageBackup=db->Prepare("SELECT img_id FROM assoc_images WHERE assoc_id=?", false);
	}
	q_getParentImageBackup->Bind(assoc_id);
	db_results res=q_getParentImageBackup->Read();
	q_getParentImageBackup->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["img_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::getImageArchived
* @return int archived
* @sql
*	SELECT archived FROM backup_images WHERE id=:backupid(int)
*/
ServerCleanupDao::CondInt ServerCleanupDao::getImageArchived(int backupid)
{
	if(q_getImageArchived==NULL)
	{
		q_getImageArchived=db->Prepare("SELECT archived FROM backup_images WHERE id=?", false);
	}
	q_getImageArchived->Bind(backupid);
	db_results res=q_getImageArchived->Read();
	q_getImageArchived->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["archived"]);
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
		ret[i]=watoi(res[i]["assoc_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<int> ServerCleanupDao::getAssocImageBackupsReverse
* @return int img_id
* @sql
*	SELECT img_id FROM assoc_images WHERE assoc_id=:assoc_id(int)
*/
std::vector<int> ServerCleanupDao::getAssocImageBackupsReverse(int assoc_id)
{
	if(q_getAssocImageBackupsReverse==NULL)
	{
		q_getAssocImageBackupsReverse=db->Prepare("SELECT img_id FROM assoc_images WHERE assoc_id=?", false);
	}
	q_getAssocImageBackupsReverse->Bind(assoc_id);
	db_results res=q_getAssocImageBackupsReverse->Read();
	q_getAssocImageBackupsReverse->Reset();
	std::vector<int> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i]=watoi(res[i]["img_id"]);
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
		ret.value=watoi64(res[0]["size_bytes"]);
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
		ret[i].id=watoi(res[i]["id"]);
		ret[i].name=res[i]["name"];
	}
	return ret;
}


/**
* @-SQLGenAccess
* @func vector<SFileBackupInfo> ServerCleanupDao::getFileBackupsOfClient
* @return int id, string backuptime, string path, int done
* @sql
*	SELECT id, backuptime, path, done FROM backups WHERE clientid=:clientid(int) ORDER BY backuptime DESC
*/
std::vector<ServerCleanupDao::SFileBackupInfo> ServerCleanupDao::getFileBackupsOfClient(int clientid)
{
	if(q_getFileBackupsOfClient==NULL)
	{
		q_getFileBackupsOfClient=db->Prepare("SELECT id, backuptime, path, done FROM backups WHERE clientid=? ORDER BY backuptime DESC", false);
	}
	q_getFileBackupsOfClient->Bind(clientid);
	db_results res=q_getFileBackupsOfClient->Read();
	q_getFileBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SFileBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i]["id"]);
		ret[i].backuptime=res[i]["backuptime"];
		ret[i].path=res[i]["path"];
		ret[i].done=watoi(res[i]["done"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDao::getOldImageBackupsOfClient
* @return int id, string backuptime, string letter, string path
* @sql
*	SELECT id, backuptime, letter, path FROM backup_images
*	WHERE clientid=:clientid(int) AND running<datetime('now','-12 hours')
*/
std::vector<ServerCleanupDao::SImageBackupInfo> ServerCleanupDao::getOldImageBackupsOfClient(int clientid)
{
	if(q_getOldImageBackupsOfClient==NULL)
	{
		q_getOldImageBackupsOfClient=db->Prepare("SELECT id, backuptime, letter, path FROM backup_images WHERE clientid=? AND running<datetime('now','-12 hours')", false);
	}
	q_getOldImageBackupsOfClient->Bind(clientid);
	db_results res=q_getOldImageBackupsOfClient->Read();
	q_getOldImageBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i]["id"]);
		ret[i].backuptime=res[i]["backuptime"];
		ret[i].letter=res[i]["letter"];
		ret[i].path=res[i]["path"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SImageBackupInfo> ServerCleanupDao::getImageBackupsOfClient
* @return int id, string backuptime, string letter, string path, int complete
* @sql
*	SELECT id, backuptime, letter, path, complete FROM backup_images WHERE clientid=:clientid(int)
*/
std::vector<ServerCleanupDao::SImageBackupInfo> ServerCleanupDao::getImageBackupsOfClient(int clientid)
{
	if(q_getImageBackupsOfClient==NULL)
	{
		q_getImageBackupsOfClient=db->Prepare("SELECT id, backuptime, letter, path, complete FROM backup_images WHERE clientid=?", false);
	}
	q_getImageBackupsOfClient->Bind(clientid);
	db_results res=q_getImageBackupsOfClient->Read();
	q_getImageBackupsOfClient->Reset();
	std::vector<ServerCleanupDao::SImageBackupInfo> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].exists=true;
		ret[i].id=watoi(res[i]["id"]);
		ret[i].backuptime=res[i]["backuptime"];
		ret[i].letter=res[i]["letter"];
		ret[i].path=res[i]["path"];
		ret[i].complete=watoi(res[i]["complete"]);
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
ServerCleanupDao::CondInt ServerCleanupDao::findFileBackup(int clientid, const std::string& path)
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
		ret.value=watoi(res[0]["id"]);
	}
	return ret;
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
		ret.value=watoi64(res[0]["used_storage"]);
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
		ret[i].id=watoi(res[i]["id"]);
		ret[i].clientid=watoi(res[i]["clientid"]);
		ret[i].incremental=watoi(res[i]["incremental"]);
		ret[i].backuptime=res[i]["backuptime"];
		ret[i].path=res[i]["path"];
		ret[i].clientname=res[i]["clientname"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SIncompleteFileBackup> ServerCleanupDao::getDeletePendingFileBackups
* @return int id, int clientid, int incremental, string backuptime, string path, string clientname
* @sql
*      SELECT b.id, b.clientid, b.incremental, b.backuptime, b.path, c.name AS clientname FROM
*		backups b INNER JOIN clients c ON b.clientid=c.id
*        WHERE b.delete_pending=1
*/
std::vector<ServerCleanupDao::SIncompleteFileBackup> ServerCleanupDao::getDeletePendingFileBackups(void)
{
	if(q_getDeletePendingFileBackups==NULL)
	{
		q_getDeletePendingFileBackups=db->Prepare("SELECT b.id, b.clientid, b.incremental, b.backuptime, b.path, c.name AS clientname FROM backups b INNER JOIN clients c ON b.clientid=c.id WHERE b.delete_pending=1", false);
	}
	db_results res=q_getDeletePendingFileBackups->Read();
	std::vector<ServerCleanupDao::SIncompleteFileBackup> ret;
	ret.resize(res.size());
	for(size_t i=0;i<res.size();++i)
	{
		ret[i].id=watoi(res[i]["id"]);
		ret[i].clientid=watoi(res[i]["clientid"]);
		ret[i].incremental=watoi(res[i]["incremental"]);
		ret[i].backuptime=res[i]["backuptime"];
		ret[i].path=res[i]["path"];
		ret[i].clientname=res[i]["clientname"];
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func vector<SHistItem> ServerCleanupDao::getClientHistory
* @return int id, string name, string lastbackup, string lastseen, string lastbackup_image, int64 bytes_used_files, int64 bytes_used_images, string max_created, int64 hist_id
* @sql
*    SELECT
*		id, name, MAX(lastbackup) AS lastbackup, MAX(lastseen) AS lastseen, MAX(lastbackup_image) AS lastbackup_image, 
*		MAX(bytes_used_files) AS bytes_used_files, MAX(bytes_used_images) AS bytes_used_images,  MAX(created) AS max_created, MAX(hist_id) AS hist_id
*	 FROM clients_hist
*    WHERE created<=date('now', :back_start(string)) AND created>date('now', :back_stop(string))
*    GROUP BY strftime(:date_grouping(string), created, 'localtime'), id, name
*/
std::vector<ServerCleanupDao::SHistItem> ServerCleanupDao::getClientHistory(const std::string& back_start, const std::string& back_stop, const std::string& date_grouping)
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
		ret[i].id=watoi(res[i]["id"]);
		ret[i].name=res[i]["name"];
		ret[i].lastbackup=res[i]["lastbackup"];
		ret[i].lastseen=res[i]["lastseen"];
		ret[i].lastbackup_image=res[i]["lastbackup_image"];
		ret[i].bytes_used_files=watoi64(res[i]["bytes_used_files"]);
		ret[i].bytes_used_images=watoi64(res[i]["bytes_used_images"]);
		ret[i].max_created=res[i]["max_created"];
		ret[i].hist_id=watoi64(res[i]["hist_id"]);
	}
	return ret;
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::deleteClientHistoryIds
* @sql
*    DELETE FROM clients_hist_id WHERE
*				 created<=date('now', :back_start(string)) AND created>date('now', :back_stop(string))
*/
void ServerCleanupDao::deleteClientHistoryIds(const std::string& back_start, const std::string& back_stop)
{
	if(q_deleteClientHistoryIds==NULL)
	{
		q_deleteClientHistoryIds=db->Prepare("DELETE FROM clients_hist_id WHERE created<=date('now', ?) AND created>date('now', ?)", false);
	}
	q_deleteClientHistoryIds->Bind(back_start);
	q_deleteClientHistoryIds->Bind(back_stop);
	q_deleteClientHistoryIds->Write();
	q_deleteClientHistoryIds->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::deleteClientHistoryItems
* @sql
*    DELETE FROM clients_hist WHERE
*				 created<=date('now', :back_start(string)) AND created>date('now', :back_stop(string))
*/
void ServerCleanupDao::deleteClientHistoryItems(const std::string& back_start, const std::string& back_stop)
{
	if(q_deleteClientHistoryItems==NULL)
	{
		q_deleteClientHistoryItems=db->Prepare("DELETE FROM clients_hist WHERE created<=date('now', ?) AND created>date('now', ?)", false);
	}
	q_deleteClientHistoryItems->Bind(back_start);
	q_deleteClientHistoryItems->Bind(back_stop);
	q_deleteClientHistoryItems->Write();
	q_deleteClientHistoryItems->Reset();
}

/**
* @-SQLGenAccess
* @func void ServerCleanupDao::insertClientHistoryId
* @sql
*    INSERT INTO clients_hist_id (created) VALUES (datetime(:created(string)))
*/
void ServerCleanupDao::insertClientHistoryId(const std::string& created)
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
*			datetime(:lastseen(string)), datetime(:lastbackup_image(string)),
*			:bytes_used_files(int64), :bytes_used_images(int64), 
*			datetime(:created(string)), :hist_id(int64) )
*/
void ServerCleanupDao::insertClientHistoryItem(int id, const std::string& name, const std::string& lastbackup, const std::string& lastseen, const std::string& lastbackup_image, int64 bytes_used_files, int64 bytes_used_images, const std::string& created, int64 hist_id)
{
	if(q_insertClientHistoryItem==NULL)
	{
		q_insertClientHistoryItem=db->Prepare("INSERT INTO clients_hist (id, name, lastbackup, lastseen, lastbackup_image, bytes_used_files, bytes_used_images, created, hist_id) VALUES (?, ?, datetime(?), datetime(?), datetime(?), ?, ?,  datetime(?), ? )", false);
	}
	q_insertClientHistoryItem->Bind(id);
	q_insertClientHistoryItem->Bind(name);
	q_insertClientHistoryItem->Bind(lastbackup);
	q_insertClientHistoryItem->Bind(lastseen);
	q_insertClientHistoryItem->Bind(lastbackup_image);
	q_insertClientHistoryItem->Bind(bytes_used_files);
	q_insertClientHistoryItem->Bind(bytes_used_images);
	q_insertClientHistoryItem->Bind(created);
	q_insertClientHistoryItem->Bind(hist_id);
	q_insertClientHistoryItem->Write();
	q_insertClientHistoryItem->Reset();
}

/**
* @-SQLGenAccess
* @func int ServerCleanupDao::hasMoreRecentFileBackup
* @return int id
* @sql
*    SELECT id FROM backups b WHERE id=:backupid(int) AND EXISTS 
*     (SELECT * FROM backups WHERE backuptime>b.backuptime 
*       AND tgroup=b.tgroup AND clientid=b.clientid AND done=1)
*/
ServerCleanupDao::CondInt ServerCleanupDao::hasMoreRecentFileBackup(int backupid)
{
	if(q_hasMoreRecentFileBackup==NULL)
	{
		q_hasMoreRecentFileBackup=db->Prepare("SELECT id FROM backups b WHERE id=? AND EXISTS  (SELECT * FROM backups WHERE backuptime>b.backuptime  AND tgroup=b.tgroup AND clientid=b.clientid AND done=1)", false);
	}
	q_hasMoreRecentFileBackup->Bind(backupid);
	db_results res=q_hasMoreRecentFileBackup->Read();
	q_hasMoreRecentFileBackup->Reset();
	CondInt ret = { false, 0 };
	if(!res.empty())
	{
		ret.exists=true;
		ret.value=watoi(res[0]["id"]);
	}
	return ret;
}

//@-SQLGenSetup
void ServerCleanupDao::createQueries(void)
{
	q_getIncompleteImages=NULL;
	q_getIncompleteImage=NULL;
	q_getDeletePendingImages=NULL;
	q_removeImage=NULL;
	q_getClientsSortFilebackups=NULL;
	q_getClientsSortImagebackups=NULL;
	q_getFullNumImages=NULL;
	q_getImageRefs=NULL;
	q_getImageRefsReverse=NULL;
	q_getImageClientId=NULL;
	q_getFileBackupClientId=NULL;
	q_getImageClientname=NULL;
	q_getImagePath=NULL;
	q_getIncrNumImages=NULL;
	q_getIncrNumImagesForBackup=NULL;
	q_getFullNumFiles=NULL;
	q_getIncrNumFiles=NULL;
	q_getClientName=NULL;
	q_getFileBackupPath=NULL;
	q_removeFileBackup=NULL;
	q_getFileBackupInfo=NULL;
	q_getImageBackupInfo=NULL;
	q_removeImageSize=NULL;
	q_addToImageStats=NULL;
	q_updateDelImageStats=NULL;
	q_getClientImages=NULL;
	q_getClientFileBackups=NULL;
	q_getParentImageBackup=NULL;
	q_getImageArchived=NULL;
	q_getAssocImageBackups=NULL;
	q_getAssocImageBackupsReverse=NULL;
	q_getImageSize=NULL;
	q_getClients=NULL;
	q_getFileBackupsOfClient=NULL;
	q_getOldImageBackupsOfClient=NULL;
	q_getImageBackupsOfClient=NULL;
	q_findFileBackup=NULL;
	q_getUsedStorage=NULL;
	q_cleanupBackupLogs=NULL;
	q_cleanupAuthLog=NULL;
	q_getIncompleteFileBackups=NULL;
	q_getDeletePendingFileBackups=NULL;
	q_getClientHistory=NULL;
	q_deleteClientHistoryIds=NULL;
	q_deleteClientHistoryItems=NULL;
	q_insertClientHistoryId=NULL;
	q_insertClientHistoryItem=NULL;
	q_hasMoreRecentFileBackup=NULL;
}

//@-SQLGenDestruction
void ServerCleanupDao::destroyQueries(void)
{
	db->destroyQuery(q_getIncompleteImages);
	db->destroyQuery(q_getIncompleteImage);
	db->destroyQuery(q_getDeletePendingImages);
	db->destroyQuery(q_removeImage);
	db->destroyQuery(q_getClientsSortFilebackups);
	db->destroyQuery(q_getClientsSortImagebackups);
	db->destroyQuery(q_getFullNumImages);
	db->destroyQuery(q_getImageRefs);
	db->destroyQuery(q_getImageRefsReverse);
	db->destroyQuery(q_getImageClientId);
	db->destroyQuery(q_getFileBackupClientId);
	db->destroyQuery(q_getImageClientname);
	db->destroyQuery(q_getImagePath);
	db->destroyQuery(q_getIncrNumImages);
	db->destroyQuery(q_getIncrNumImagesForBackup);
	db->destroyQuery(q_getFullNumFiles);
	db->destroyQuery(q_getIncrNumFiles);
	db->destroyQuery(q_getClientName);
	db->destroyQuery(q_getFileBackupPath);
	db->destroyQuery(q_removeFileBackup);
	db->destroyQuery(q_getFileBackupInfo);
	db->destroyQuery(q_getImageBackupInfo);
	db->destroyQuery(q_removeImageSize);
	db->destroyQuery(q_addToImageStats);
	db->destroyQuery(q_updateDelImageStats);
	db->destroyQuery(q_getClientImages);
	db->destroyQuery(q_getClientFileBackups);
	db->destroyQuery(q_getParentImageBackup);
	db->destroyQuery(q_getImageArchived);
	db->destroyQuery(q_getAssocImageBackups);
	db->destroyQuery(q_getAssocImageBackupsReverse);
	db->destroyQuery(q_getImageSize);
	db->destroyQuery(q_getClients);
	db->destroyQuery(q_getFileBackupsOfClient);
	db->destroyQuery(q_getOldImageBackupsOfClient);
	db->destroyQuery(q_getImageBackupsOfClient);
	db->destroyQuery(q_findFileBackup);
	db->destroyQuery(q_getUsedStorage);
	db->destroyQuery(q_cleanupBackupLogs);
	db->destroyQuery(q_cleanupAuthLog);
	db->destroyQuery(q_getIncompleteFileBackups);
	db->destroyQuery(q_getDeletePendingFileBackups);
	db->destroyQuery(q_getClientHistory);
	db->destroyQuery(q_deleteClientHistoryIds);
	db->destroyQuery(q_deleteClientHistoryItems);
	db->destroyQuery(q_insertClientHistoryId);
	db->destroyQuery(q_insertClientHistoryItem);
	db->destroyQuery(q_hasMoreRecentFileBackup);
}