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

#include "../Interface/Database.h"
#include "../Interface/Server.h"
#include "../Interface/DatabaseCursor.h"
#include "../Interface/File.h"
#include "database.h"
#include "server_settings.h"
#include "LMDBFileIndex.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "serverinterface/helper.h"
#include "dao/ServerBackupDao.h"

namespace
{
const size_t sqlite_data_allocation_chunk_size = 50 * 1024 * 1024; //50MB

struct SCallbackData
{
	IDatabaseCursor* cur;
	int64 pos;
	int64 max_pos;
	SStartupStatus* status;
};

db_results create_callback(size_t n_done, size_t n_rows, void *userdata)
{
	SCallbackData *data=(SCallbackData*)userdata;

	data->status->processed_file_entries=n_done;
	
	int last_pc = static_cast<int>(data->status->pc_done*1000 + 0.5);
	
	if(data->max_pos>0)
	{
		data->status->pc_done = static_cast<double>(n_rows)/data->max_pos;
	}
	
	int curr_pc = static_cast<int>(data->status->pc_done*1000 + 0.5);
	
	if(curr_pc!=last_pc)
	{
		Server->Log("Creating files index: "+convert((double)curr_pc/10)+"% finished", LL_INFO);
	}
	
	db_results ret;
	db_single_result res;
	
	if(data->cur->next(res))
	{
		ret.push_back(res);
	}
	
	return ret;
}

bool create_files_index_common(FileIndex& fileindex, SStartupStatus& status)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);

	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		Server->Log("Deleting database journal...", LL_INFO);
		db->Write("PRAGMA journal_mode = DELETE");

		if (FileExists("urbackup/backup_server_files.db-journal")
			|| FileExists("urbackup/backup_server_files.db-wal"))
		{
			Server->Log("Deleting database journal failed. Aborting.", LL_ERROR);
			return false;
		}

		Server->destroyAllDatabases();

		Server->deleteFile("urbackup/backup_server_files_new.db");

		Server->Log("Copying/reflinking database...", LL_INFO);
		if (!os_create_hardlink("urbackup/backup_server_files_new.db",
			"urbackup/backup_server_files.db", true, NULL))
		{
			Server->Log("Reflinking failed. Falling back to copying...", LL_DEBUG);

			if (!copy_file("urbackup/backup_server_files.db",
				"urbackup/backup_server_files_new.db"))
			{
				Server->Log("Copying file failed. " + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		str_map params;
		if (!Server->openDatabase("urbackup/backup_server_files_new.db", URBACKUPDB_SERVER_FILES_NEW, params))
		{
			Server->Log("Couldn't open Database backup_server_files_new.db. Exiting. Expecting database at \"" +
				Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_files_new.db\"", LL_ERROR);
			return false;
		}

		Server->setDatabaseAllocationChunkSize(URBACKUPDB_SERVER_FILES_NEW, sqlite_data_allocation_chunk_size);

		db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES_NEW);
		if (db==NULL)
		{
			Server->Log("Couldn't open backup server database. Exiting. Expecting database at \"" +
				Server->getServerWorkingDir() + os_file_sep() + "urbackup" + os_file_sep() + "backup_server_files_new.db\"", LL_ERROR);
			return false;
		}

		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+convert(server_settings.getSettings()->update_stats_cachesize));

		db->Write("PRAGMA journal_mode = OFF");
		db->Write("PRAGMA synchronous = OFF");
	}

	status.creating_filesindex=true;
	Server->Log("Creating file entry index. This might take a while...", LL_WARNING);
	
	Server->Log("Getting number of files...", LL_INFO);
	
	db_results res = db->Read("SELECT COUNT(*) AS c FROM files");
	
	int64 n_files = 0;
	if(!res.empty())
	{
		n_files=watoi64(res[0]["c"]);
	}

	Server->Log("Dropping index...", LL_INFO);

	db->Write("DROP INDEX IF EXISTS files_backupid");


	Server->Log("Starting creating files index...", LL_INFO);

	IQuery *q_read=db->Prepare("SELECT id, shahash, filesize, clientid, next_entry, prev_entry, pointed_to FROM files ORDER BY shahash ASC, filesize ASC, clientid ASC, created DESC");

	SCallbackData data;
	data.cur=q_read->Cursor();
	data.pos=0;
	data.max_pos=n_files;
	data.status=&status;

	fileindex.create(create_callback, &data);

	if(fileindex.has_error())
	{
		return false;
	}
	else
	{
		if (data.cur->has_error())
		{
			return false;
		}

		Server->Log("Creating backupid index...", LL_INFO);

		db->Write("CREATE INDEX files_backupid ON files (backupid)");

		Server->Log("Copying back result...", LL_INFO);

		Server->destroyAllDatabases();

		std::auto_ptr<IFile> db_file(Server->openFile("urbackup/backup_server_files_new.db", MODE_RW));

		if (db_file.get() == NULL)
		{
			Server->Log("Error opening new database file", LL_ERROR);
			return false;
		}

		db_file->Sync();
		db_file.reset();

		if (!os_create_hardlink("urbackup/backup_server_files.db",
			"urbackup/backup_server_files_new.db", true, NULL))
		{
			Server->Log("Reflinking failed. Falling back to copying...", LL_DEBUG);

			if (!copy_file("urbackup/backup_server_files_new.db",
				"urbackup/backup_server_files.db"))
			{
				Server->Log("Copying file failed. " + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		Server->deleteFile("urbackup/backup_server_files_new.db");

		db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);

		if (db == NULL)
		{
			Server->Log("Error opening database after file index creation.", LL_ERROR);
			return false;
		}

		db->Write("PRAGMA journal_mode = WAL");
	}

	status.creating_filesindex=false;

	return true;
}

bool setup_lmdb_file_index(SStartupStatus& status)
{
	LMDBFileIndex fileindex(true);;
	if(fileindex.has_error())
	{
		Server->Log("Error creating file index", LL_ERROR);
		return false;
	}

	return create_files_index_common(fileindex, status);
}

}

void delete_file_index(void)
{
	Server->deleteFile("urbackup/fileindex/backup_server_files_index.lmdb");
	Server->deleteFile("urbackup/fileindex/backup_server_files_index.lmdb-lock");
}

bool create_files_index(SStartupStatus& status)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	bool creating_index;

	{
		ServerBackupDao backupdao(db);
		creating_index = backupdao.getMiscValue("creating_file_entry_index").value == "true";
	}

	if(!FileExists("urbackup/fileindex/backup_server_files_index.lmdb") || creating_index)
	{
		delete_file_index();

		{
			DBScopedSynchronous synchronous_db(db);

			ServerBackupDao backupdao(db);

			backupdao.delMiscValue("creating_file_entry_index");
			backupdao.addMiscValue("creating_file_entry_index", "true");
		}

		status.upgrading_database=false;
		status.creating_filesindex=true;

		if(!setup_lmdb_file_index(status))
		{
			Server->Log("Setting up file index failed", LL_ERROR);
			return false;
		}
		else
		{
			db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
			ServerBackupDao backupdao(db);
			backupdao.delMiscValue("creating_file_entry_index");
		}
	}
	
	LMDBFileIndex::initFileIndex();

	return true;

}

FileIndex* create_lmdb_files_index(void)
{
	if(!FileExists("urbackup/fileindex/backup_server_files_index.lmdb"))
	{
		return NULL;
	}

	return new LMDBFileIndex();
}