#include "../Interface/Database.h"
#include "../Interface/Server.h"
#include "../Interface/DatabaseCursor.h"
#include "database.h"
#include "server_settings.h"
#include "LMDBFileIndex.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "serverinterface/helper.h"
#include "dao/ServerBackupDao.h"

namespace
{

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
		Server->Log("Creating files index: "+nconvert((double)curr_pc/10)+"% finished", LL_INFO);
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
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));

		Server->Log("Transitioning urbackup server database to different journaling mode...", LL_INFO);
		db->Write("PRAGMA journal_mode = DELETE");
	}

	status.creating_filesindex=true;
	Server->Log("Creating file entry index. This might take a while...", LL_WARNING);
	
	Server->Log("Getting number of files...", LL_INFO);
	
	db_results res = db->Read("SELECT COUNT(*) AS c FROM files");
	
	int64 n_files = 0;
	if(!res.empty())
	{
		n_files=watoi64(res[0][L"c"]);
	}

	db->BeginTransaction();
	
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
		db->Write("ROLLBACK");
		return false;
	}
	else
	{
		db->EndTransaction();
	}

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
		db->Write("PRAGMA shrink_memory");
		db->Write("PRAGMA journal_mode = WAL");
	}

	ServerBackupDao backupdao(db);

	Server->Log("Committing to database...", LL_INFO);
	backupdao.commit();
	Server->Log("Committing done.", LL_INFO);

	status.creating_filesindex=false;

	if(data.cur->has_error())
	{
		return false;
	}

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
	ServerSettings settings(db);

	ServerBackupDao backupdao(db);

	bool creating_index = backupdao.getMiscValue(L"creating_file_entry_index").value==L"true";

	if(!FileExists("urbackup/fileindex/backup_server_files_index.lmdb") || creating_index)
	{
		delete_file_index();

		backupdao.delMiscValue(L"creating_file_entry_index");
		backupdao.addMiscValue(L"creating_file_entry_index", L"true");
		backupdao.commit();

		status.upgrading_database=false;
		status.creating_filesindex=true;

		if(!setup_lmdb_file_index(status))
		{
			Server->Log("Setting up file index failed", LL_ERROR);
			return false;
		}
		else
		{
			backupdao.delMiscValue(L"creating_file_entry_index");
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