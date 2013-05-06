#include "ServerCleanupDAO.h"
#include "../../stringtools.h"

ServerCleanupDAO::ServerCleanupDAO(IDatabase *db)
	: db(db)
{
}

ServerCleanupDAO::~ServerCleanupDAO(void)
{
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
}