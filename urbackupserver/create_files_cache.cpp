#include "../Interface/Database.h"
#include "../Interface/Server.h"
#include "../Interface/DatabaseCursor.h"
#include "database.h"
#include "server_settings.h"
#include "MDBFileCache.h"
#include "../stringtools.h"

namespace
{

std::wstring get_files_cache_type(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	db_results res=db->Read("SELECT tvalue FROM misc WHERE tkey='files_cache'");

	if(!res.empty())
	{
		return res[0][L"tvalue"];
	}
	return std::wstring();
}

void update_files_cache_type(std::string new_type)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	IQuery *q=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='files_cache'");

	q->Bind(new_type);
	q->Write();
}

struct SCallbackData
{
	IDatabaseCursor* cur;
	int64 pos;
};

db_results create_callback(void *userdata)
{
	SCallbackData *data=(SCallbackData*)userdata;

	
	db_results ret;
	db_single_result res;
	
	if(data->cur->next(res))
	{
		ret.push_back(res);
	}
	
	return ret;
}

bool setup_lmdb_files_cache(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db_results cache_res;
	if(db->getEngineName()=="sqlite")
	{
		cache_res=db->Read("PRAGMA cache_size");
		db->Write("PRAGMA cache_size = -"+nconvert(500*1024));
	}

	Server->Log("Creating LMDB file entry cache. This might take a while...", LL_WARNING);

	IQuery *q_read=db->Prepare("SELECT shahash, filesize, fullpath, hashpath, created FROM files f WHERE f.created=(SELECT MAX(created) FROM files WHERE shahash=f.shahash AND filesize=f.filesize) ORDER BY shahash ASC, filesize ASC");

	SCallbackData data;
	data.cur=q_read->Cursor();
	data.pos=0;

	MDBFileCache filecache;
	if(filecache.has_error())
	{
		Server->Log("Error creating file cache", LL_ERROR);
		return false;
	}

	filecache.create(create_callback, &data);

	if(!cache_res.empty())
	{
		db->Write("PRAGMA cache_size = "+wnarrow(cache_res[0][L"cache_size"]));
		db->Write("PRAGMA shrink_memory");
	}

	return true;
}

}

void create_files_cache(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerSettings settings(db);

	if(settings.getSettings()->filescache_type=="lmdb")
	{
		MDBFileCache::initFileCache(static_cast<size_t>(settings.getSettings()->filescache_size));
	}

	if(get_files_cache_type()==L"none" && settings.getSettings()->filescache_type=="lmdb")
	{
		if(setup_lmdb_files_cache())
		{
			update_files_cache_type(settings.getSettings()->filescache_type);
		}
	}

	if(settings.getSettings()->filescache_type=="none")
	{
		if(FileExists("urbackup/backup_server_files_cache.lmdb"))
		{
			Server->deleteFile("urbackup/backup_server_files_cache.lmdb");
			Server->deleteFile("urbackup/backup_server_files_cache.lmdb-lock");
		}

		update_files_cache_type(settings.getSettings()->filescache_type);
	}
}

FileCache* create_lmdb_files_cache(void)
{
	return new MDBFileCache();
}