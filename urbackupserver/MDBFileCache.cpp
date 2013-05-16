#include "MDBFileCache.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "../common/data.h"

MDB_env *MDBFileCache::env=NULL;


void MDBFileCache::initFileCache(size_t map_size)
{
	MDBFileCache* filecache=new MDBFileCache(map_size);
	delete filecache;
}

MDBFileCache::MDBFileCache(size_t map_size)
	: _has_error(false)
{
	int rc;
	if(env==NULL)
	{
		rc = mdb_env_create(&env);
		if(rc)
		{
			Server->Log("LMDB: Failed to create LMDB env ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			_has_error=true;
			return;
		}

		rc = mdb_env_set_mapsize(env, map_size);

		if(rc)
		{
			Server->Log("LMDB: Failed to set map size ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			_has_error=true;
			return;
		}

		rc = mdb_env_open(env, "urbackup/backup_server_files_cache.lmdb", MDB_NOSUBDIR|MDB_NOMETASYNC|MDB_NOSYNC, 0664);

		if(rc)
		{
			Server->Log("LMDB: Failed to open LMDB database file ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			_has_error=true;
			return;
		}
	}
}

MDBFileCache::~MDBFileCache(void)
{
}


bool MDBFileCache::has_error(void)
{
	return _has_error;
}

void MDBFileCache::begin_txn(unsigned int flags)
{
	int rc = mdb_txn_begin(env, NULL, flags, &txn);

	if(rc)
	{
		Server->Log("LMDB: Failed to open transaction handle ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
		return;
	}

	rc = mdb_open(txn, NULL, 0, &dbi);

	if(rc)
	{
		Server->Log("LMDB: Failed to open database ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
		return;
	}
}

void MDBFileCache::create(get_data_callback_t get_data_callback, void *userdata)
{
	begin_txn(0);

	db_results res;
	do
	{
		res=get_data_callback(userdata);

		for(size_t i=0;i<res.size();++i)
		{
			const std::wstring& shahash=res[i][L"shahash"];
			SCacheKey key(reinterpret_cast<const char*>(shahash.c_str()), watoi64(res[i][L"filesize"]));

			CWData value;
			value.addString(Server->ConvertToUTF8(res[i][L"fullpath"]));
			value.addString(Server->ConvertToUTF8(res[i][L"hashpath"]));
			
			MDB_val mdb_tkey;
			mdb_tkey.mv_data=&key;
			mdb_tkey.mv_size=sizeof(SCacheKey);

			MDB_val mdb_tvalue;
			mdb_tvalue.mv_data=value.getDataPtr();
			mdb_tvalue.mv_size=value.getDataSize();

			int rc = mdb_put(txn, dbi, &mdb_tkey, &mdb_tvalue, 0);

			if(rc)
			{
				Server->Log("LMDB: Failed to put data ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
				_has_error=true;
			}
		}
	}
	while(!res.empty());

	int rc = mdb_txn_commit(txn);
	
	if(rc)
	{
		Server->Log("LMDB: Failed to commit transaction ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}
}

MDBFileCache::SCacheValue MDBFileCache::get(const MDBFileCache::SCacheKey& key)
{
	begin_txn(MDB_RDONLY);

	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SCacheKey);

	MDB_val mdb_tvalue;

	int rc=mdb_get(txn, dbi, &mdb_tkey, &mdb_tvalue);

	MDBFileCache::SCacheValue ret;
	if(rc==MDB_NOTFOUND)
	{
		
	}
	else if(rc)
	{
		Server->Log("LMDB: Failed to read ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}
	else
	{
		CRData data((const char*)mdb_tvalue.mv_data, mdb_tvalue.mv_size);
		
		ret.exists=true;

		data.getStr(&ret.fullpath);
		data.getStr(&ret.hashpath);
	}

	mdb_txn_abort(txn);

	return ret;
}

void MDBFileCache::start_transaction(void)
{
	begin_txn(0);
}

void MDBFileCache::put(const MDBFileCache::SCacheKey& key, const MDBFileCache::SCacheValue& value)
{
	CWData vdata;
	vdata.addString(value.fullpath);
	vdata.addString(value.hashpath);
			
	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SCacheKey);

	MDB_val mdb_tvalue;
	mdb_tvalue.mv_data=vdata.getDataPtr();
	mdb_tvalue.mv_size=vdata.getDataSize();

	int rc = mdb_put(txn, dbi, &mdb_tkey, &mdb_tvalue, 0);

	if(rc)
	{
		Server->Log("LMDB: Failed to put data ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}
}

void MDBFileCache::del(const SCacheKey& key)
{		
	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SCacheKey);

	int rc = mdb_del(txn, dbi, &mdb_tkey, NULL);

	if(rc)
	{
		Server->Log("LMDB: Failed to delete data ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}
}

void MDBFileCache::commit_transaction(void)
{
	int rc = mdb_txn_commit(txn);
	
	if(rc)
	{
		Server->Log("LMDB: Failed to commit transaction ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}
}