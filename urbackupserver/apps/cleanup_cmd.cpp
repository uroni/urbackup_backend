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
#include "../server_settings.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../stringtools.h"
#include "../server_cleanup.h"
#include "../server.h"
#include "../serverinterface/helper.h"
#include "../create_files_index.h"

extern SStartupStatus startup_status;


int64 cleanup_amount(std::string cleanup_pc, IDatabase *db)
{
	ServerSettings settings(db);

	int64 total_space=os_total_space(settings.getSettings()->backupfolder);

	if(total_space==-1)
	{
		Server->Log("Error getting free space", LL_ERROR);
		return -1;
	}

	strupper(&cleanup_pc);

	int64 cleanup_bytes=0;
	if(cleanup_pc.find("%")!=std::string::npos)
	{
		double pc=atof(getuntil("%", cleanup_pc).c_str());
		Server->Log("Cleaning up "+convert(pc)+" percent", LL_INFO);

		cleanup_bytes=(int64)((pc/100)*total_space+0.5);
	}
	else if(cleanup_pc.find("K")!=std::string::npos)
	{
		cleanup_bytes=watoi64(getuntil("K", cleanup_pc))*1024;
	}
	else if(cleanup_pc.find("M")!=std::string::npos)
	{
		cleanup_bytes=watoi64(getuntil("M", cleanup_pc))*1024*1024;
	}
	else if(cleanup_pc.find("G")!=std::string::npos)
	{
		cleanup_bytes=watoi64(getuntil("G", cleanup_pc))*1024*1024*1024;
	}
	else if(cleanup_pc.find("T")!=std::string::npos)
	{
		cleanup_bytes=watoi64(getuntil("T", cleanup_pc))*1024*1024*1024*1024;
	}
	else
	{
		cleanup_bytes=watoi64(cleanup_pc);
	}

	if(cleanup_bytes>total_space)
	{
		cleanup_bytes=total_space;
	}

	return cleanup_bytes;
}

int cleanup_cmd(void)
{
	Server->Log("Shutting down all database instances...", LL_INFO);
	Server->destroyAllDatabases();

	Server->Log("Opening urbackup server database...", LL_INFO);
	open_server_database(true);
	open_settings_database();

	if(!create_files_index(startup_status))
	{
		Server->Log("Error opening files index...", LL_INFO);
		return 2;
	}

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
		Server->Log("Could not open database", LL_ERROR);
		return 1;
	}

	BackupServer::testSnapshotAvailability(db);

	Server->Log("Transitioning urbackup server database to different journaling mode...", LL_INFO);
	db->Write("PRAGMA journal_mode = DELETE");

	std::string cleanup_pc=Server->getServerParameter("cleanup_amount");

	if(cleanup_pc=="true" || cleanup_pc.empty())
	{
		cleanup_pc="10%";
	}

	int64 cleanup_bytes=cleanup_amount(cleanup_pc, db);

	if(cleanup_bytes<0)
	{
		return 3;
	}

	Server->Log("Cleaning up "+PrettyPrintBytes(cleanup_bytes)+" on backup storage", LL_INFO);

	{
		ServerSettings settings(db);
		size_t cache_size=settings.getSettings()->update_stats_cachesize;
		Server->Log("Database cache size is "+PrettyPrintBytes(cache_size*1024), LL_INFO);
	}

	Server->Log("Starting cleanup...", LL_INFO);

	Server->Log("Freeing database connections...", LL_INFO);

	Server->destroyAllDatabases();

	bool b = ServerCleanupThread::cleanupSpace(cleanup_bytes, true);

	if(!b)
	{
		Server->Log("Cleanup failed. Could not remove backups. Please lower the minimal number of backups", LL_ERROR);
		return 2;
	}

	Server->Log("Cleanup successfull.", LL_INFO);

	return 0;
}

int defrag_database(void)
{
	Server->Log("Shutting down all database instances...", LL_INFO);
	Server->destroyAllDatabases();

	Server->Log("Opening urbackup server database...", LL_INFO);
	open_server_database(true);

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
		Server->Log("Could not open database", LL_ERROR);
		return 1;
	}

	Server->Log("Transitioning urbackup server database to different journaling mode...", LL_INFO);
	db->Write("PRAGMA journal_mode = DELETE");

	Server->Log("Rebuilding Database...", LL_INFO);
	db->Write("PRAGMA page_size = 32768");
	db->Write("VACUUM");

	Server->Log("Rebuilding Database successfull.", LL_INFO);

	Server->Log("Deleting file entry index, if present...", LL_INFO);

	delete_file_index();

	Server->Log("Done.");

	return 0;
}

int remove_unknown(void)
{
	Server->Log("Going to remove all unknown files and directories in the urbackup storage directory. Waiting 20 seconds...", LL_INFO);

	Server->wait(20000);

	Server->setServerParameter("cleanup_amount", "0%");
	if(cleanup_cmd()!=0)
	{
		Server->Log("Error cleaning up.", LL_ERROR);
		return 1;
	}

	ServerCleanupThread::removeUnknown();

	Server->Log("Successfully removed all unknown files in backup directory.", LL_INFO);

	return 0;
}

int cleanup_database(void)
{
	Server->Log("Going to clean up unnessecary information in database. Waiting 20 seconds...", LL_INFO);

	Server->wait(20000);

	Server->Log("Shutting down all database instances...", LL_INFO);
	Server->destroyAllDatabases();

	Server->Log("Opening urbackup server database...", LL_INFO);
	open_server_database(true);
	open_settings_database();

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
		Server->Log("Could not open database", LL_ERROR);
		return 1;
	}

	Server->Log("Transitioning urbackup server database to different journaling mode...", LL_INFO);
	db->Write("PRAGMA journal_mode = DELETE");

	db_results res=db->Read("SELECT * FROM sqlite_master WHERE type='table'");

	for(size_t i=0;i<res.size();++i)
	{
		db_results rc=db->Read("SELECT count(*) AS c FROM "+res[i]["name"]);
		if(!rc.empty())
		{
			Server->Log("Table "+res[i]["name"]+" has "+rc[0]["c"]+" rows", LL_INFO);
		}
	}

	Server->Log("Cleaning up information...", LL_INFO);
	db_results rc=db->Read("SELECT count(*) AS c FROM del_stats");
	if(!rc.empty())
	{
		if(watoi64(rc[0]["c"])>10000000)
		{
			db->Write("DELETE FROM del_stats");
		}
	}

	Server->Log("Cleaning up database...", LL_INFO);
	db->Write("VACUUM");

	return 0;
}
