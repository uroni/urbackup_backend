#include "../Interface/Database.h"
#include "../Interface/Server.h"
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
	IQuery* q_read;
	int64 pos;
};

db_results create_callback(void *userdata)
{
	SCallbackData *data=(SCallbackData*)userdata;

	data->q_read->Bind(data->pos);
	db_results res=data->q_read->Read();
	data->q_read->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		int64 rowid=watoi64(res[i][L"rowid"]);
		if(rowid>data->pos)
		{
			data->pos=rowid;
		}
	}

	return res;
}

bool setup_lmdb_files_cache(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	SCallbackData data;
	data.q_read=db->Prepare("SELECT rowid, shahash, filesize, fullpath, hashpath FROM files f WHERE rowid>? AND f.created=(SELECT MAX(created) FROM files WHERE shahash=f.shahash AND filesize=f.filesize) LIMIT 10000");
	data.pos=0;

	MDBFileCache filecache;
	if(filecache.has_error())
	{
		Server->Log("Error creating file cache", LL_ERROR);
		return false;
	}

	filecache.create(create_callback, &data);

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