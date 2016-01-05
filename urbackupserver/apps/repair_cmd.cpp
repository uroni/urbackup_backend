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

#include "app.h"

void open_settings_database_full()
{
	if(! Server->openDatabase("urbackup/backup_server_settings.db", URBACKUPDB_SERVER_SETTINGS, "sqlite") )
	{
		Server->Log("Couldn't open Database backup_server_settings.db. Exiting.", LL_ERROR);
		exit(1);
	}
}

int repair_cmd(void)
{
	open_server_database(true);
	open_settings_database_full();

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

	db_settings=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_SETTINGS);
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