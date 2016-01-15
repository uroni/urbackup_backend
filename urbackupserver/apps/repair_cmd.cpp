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
#include "../../stringtools.h"

void open_settings_database_full()
{
	if(! Server->openDatabase("urbackup/backup_server_settings.db", URBACKUPDB_SERVER_SETTINGS) )
	{
		Server->Log("Couldn't open Database backup_server_settings.db. Exiting.", LL_ERROR);
		exit(1);
	}
}

int repair_cmd(void)
{
	open_server_database(true);
	open_settings_database_full();

	std::vector<DATABASE_ID> dbs;
	dbs.push_back(URBACKUPDB_SERVER);
	dbs.push_back(URBACKUPDB_SERVER_SETTINGS);
	dbs.push_back(URBACKUPDB_SERVER_FILES);
	dbs.push_back(URBACKUPDB_SERVER_LINKS);
	dbs.push_back(URBACKUPDB_SERVER_LINK_JOURNAL);

	for (size_t i = 0; i < dbs.size(); ++i)
	{
		IDatabase *db = Server->getDatabase(Server->getThreadID(), dbs[i]);
		if (db == NULL)
		{
			Server->Log("Could not open database with id "+convert(dbs[i]), LL_ERROR);
			return 1;
		}

		Server->Log("Exporting database with id " + convert(dbs[i])+"...", LL_INFO);
		if (!db->Dump("urbackup/server_database_export_"+ convert(dbs[i])+".sql"))
		{
			Server->Log("Exporting database failed", LL_ERROR);
			return 1;
		}
	}
	

	Server->destroyAllDatabases();

	std::vector<std::string> db_names;
	db_names.push_back("");
	db_names.push_back("settings");
	db_names.push_back("files");
	db_names.push_back("links");
	db_names.push_back("link_journal");

	for (size_t i = 0; i < db_names.size(); ++i)
	{
		Server->deleteFile("urbackup/backup_server"+db_names[i]+".db");
		Server->deleteFile("urbackup/backup_server" + db_names[i] + ".db-wal");
		Server->deleteFile("urbackup/backup_server" + db_names[i] + ".db-shm");
	}

	for (size_t i = 0; i < dbs.size(); ++i)
	{
		IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		if (db == NULL)
		{
			Server->Log("Could not open database with id " + convert(dbs[i]), LL_ERROR);
			return 1;
		}

		Server->Log("Importing database with id " + convert(dbs[i]) + "...", LL_INFO);
		if (!db->Import("urbackup/server_database_export_" + convert(dbs[i]) + ".sql"))
		{
			Server->Log("Importing database failed", LL_ERROR);
			return 1;
		}

		Server->deleteFile("urbackup/server_database_export_" + convert(dbs[i]) + ".sql");
	}

	return 0;
}