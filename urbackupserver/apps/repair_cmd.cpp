#include "../../Interface/Server.h"
#include "../../Interface/Database.h"
#include "../database.h"

void open_server_database(bool &use_berkeleydb, bool init_db);

const DATABASE_ID URBACKUPDB_SERVER_SETTINGS=30;

void open_settings_database_full(bool use_berkeleydb)
{
	if(!use_berkeleydb)
	{
		if(! Server->openDatabase("urbackup/backup_server_settings.db", URBACKUPDB_SERVER_SETTINGS, "sqlite") )
		{
			Server->Log("Couldn't open Database backup_server_settings.db", LL_ERROR);
			return;
		}
	}
	else
	{
		if(! Server->openDatabase("urbackup/backup_server_settings.bdb", URBACKUPDB_SERVER_SETTINGS, "bdb") )
		{
			Server->Log("Couldn't open Database backup_server_settings.bdb", LL_ERROR);
			return;
		}
	}
}

int repair_cmd(void)
{
	bool use_berkeleydb;
	open_server_database(use_berkeleydb, true);
	open_settings_database_full(use_berkeleydb);

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
		Server->Log("Could not open main database", LL_ERROR);
		return 1;
	}

	Server->Log("Exporting main database...", LL_INFO);
	if(!db->Dump("urbackup/server_database_export_main.sql"))
	{
		Server->Log("Exporting main database failed", LL_ERROR);
		return 1;
	}

	IDatabase *db_settings=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_SETTINGS);
	if(db_settings==NULL)
	{
		Server->Log("Could not open settings database", LL_ERROR);
		return 1;
	}

	Server->Log("Exporting settings database...", LL_INFO);
	if(!db_settings->Dump("urbackup/server_database_export_settings.sql"))
	{
		Server->Log("Exporting settings database failed", LL_ERROR);
		return 1;
	}

	Server->destroyAllDatabases();
	db=NULL;

	Server->deleteFile("urbackup/backup_server.db");
	Server->deleteFile("urbackup/backup_server.db-wal");
	Server->deleteFile("urbackup/backup_server.db-shm");

	Server->deleteFile("urbackup/backup_server_settings.db");
	Server->deleteFile("urbackup/backup_server_settings.db-wal");
	Server->deleteFile("urbackup/backup_server_settings.db-shm");

	
	open_server_database(use_berkeleydb, false);
	open_settings_database_full(use_berkeleydb);

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
		Server->Log("Could not open main database", LL_ERROR);
		return 1;
	}

	Server->Log("Importing main database...", LL_INFO);
	if(!db->Import("urbackup/server_database_export_main.sql"))
	{
		Server->Log("Importing main database failed", LL_ERROR);
		return 1;
	}

	db_settings=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db_settings==NULL)
	{
		Server->Log("Could not open settings database", LL_ERROR);
		return 1;
	}

	Server->Log("Importing settings database...", LL_INFO);
	if(!db_settings->Import("urbackup/server_database_export_settings.sql"))
	{
		Server->Log("Importing settings database failed", LL_ERROR);
		return 1;
	}

	Server->deleteFile("urbackup/server_database_export_main.sql");
	Server->deleteFile("urbackup/server_database_export_settings.sql");

	Server->Log("Completed sucessfully.", LL_INFO);

	return 0;
}