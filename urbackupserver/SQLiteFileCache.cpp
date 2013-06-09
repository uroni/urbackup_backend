#include "SQLiteFileCache.h"
#include "../Interface/Server.h"
#include "database.h"
#include "../stringtools.h"
#include "../common/data.h"

void SQLiteFileCache::initFileCache(void)
{
	SQLiteFileCache* filecache=new SQLiteFileCache;
	Server->createThread(filecache);
}

SQLiteFileCache::SQLiteFileCache(void)
	:  _has_error(false)
{
	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_FILES_CACHE);
	setup_queries();
}

SQLiteFileCache::~SQLiteFileCache(void)
{
	db->destroyQuery(q_put);
	db->destroyQuery(q_get);
	db->destroyQuery(q_del);
}

bool SQLiteFileCache::has_error(void)
{
	return db==NULL || _has_error;
}

void SQLiteFileCache::setup_queries(void)
{
	q_put=db->Prepare("INSERT INTO files_cache (key, value) VALUES (?, ?)", false);
	q_del=db->Prepare("DELETE FROM files_cache WHERE key=?", false);
	q_get=db->Prepare("SELECT value FROM files_cache WHERE key=?", false);
}

void SQLiteFileCache::create(get_data_callback_t get_data_callback, void *userdata)
{
	setup_queries();

	db->BeginTransaction();

	size_t n_done=0;

	SCacheKey last;
	db_results res;
	do
	{
		res=get_data_callback(userdata);

		for(size_t i=0;i<res.size();++i)
		{
			const std::wstring& shahash=res[i][L"shahash"];
			SCacheKey key(reinterpret_cast<const char*>(shahash.c_str()), watoi64(res[i][L"filesize"]));

			if(key==last)
			{
				continue;
			}

			last=key;
			
			CWData value;
			value.addString(Server->ConvertToUTF8(res[i][L"fullpath"]));
			value.addString(Server->ConvertToUTF8(res[i][L"hashpath"]));
			
			++n_done;

			q_put->Bind((char*)&key, sizeof(SCacheKey));
			q_put->Bind(value.getDataPtr(), value.getDataSize());
			if(!q_put->Write())
			{
				Server->Log("SQLiteCache: Failed to put data", LL_ERROR);
			}
			q_put->Reset();

			if(n_done % 10000 == 0 )
			{
				Server->Log("File entry cache contains "+nconvert(n_done)+" entries now.", LL_INFO);
			}
		}		
	}
	while(!res.empty());

	if(!db->EndTransaction())
	{
		Server->Log("SQLiteCache: Failed to commit transaction", LL_ERROR);
		_has_error=true;
	}

	db->Write("CREATE INDEX files_cache_idx ON files_cache (key)");
}

FileCache::SCacheValue SQLiteFileCache::get(const SCacheKey& key)
{
	q_get->Bind((char*)&key, sizeof(SCacheKey));
	db_results res=q_get->Read();
	q_get->Reset();
	FileCache::SCacheValue ret;
	if(!res.empty())
	{
		ret.exists=true;

		std::wstring& strdata=res[0][L"value"];

		CRData data((const char*)strdata.c_str(), strdata.size()*sizeof(wchar_t));
		
		data.getStr(&ret.fullpath);
		data.getStr(&ret.hashpath);
	}
	else
	{
		ret.exists=false;
	}

	return ret;
}

void SQLiteFileCache::start_transaction(void)
{
	db->BeginTransaction();
}

void SQLiteFileCache::put(const SCacheKey& key, const SCacheValue& value)
{
	q_put->Bind((char*)&key, sizeof(SCacheKey));

	CWData data;
	data.addString(value.fullpath);
	data.addString(value.hashpath);

	q_put->Bind(data.getDataPtr(), data.getDataSize());

	q_put->Write();
	q_put->Reset();
}

void SQLiteFileCache::del(const SCacheKey& key)
{
	q_del->Bind((char*)&key, sizeof(SCacheKey));
	q_del->Write();
	q_del->Reset();
}

void SQLiteFileCache::commit_transaction(void)
{
	db->EndTransaction();
}