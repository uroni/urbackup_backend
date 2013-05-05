#include "ServerCleanupDAO.h"

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

//@-SQLGenSetup(db)
void ServerCleanupDAO::createQueries(void)
{
	q_incomplete_images=db->Prepare("SELECT id, path FROM backup_images WHERE complete=0 AND running<datetime('now','-300 seconds')", false);
}

//@-SQLGenDestruction(db)
void ServerCleanupDAO::destroyQueries(void)
{
	db->destroyQuery(q_incomplete_images);
}