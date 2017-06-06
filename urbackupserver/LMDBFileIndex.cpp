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

#include "LMDBFileIndex.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"
#include "../common/data.h"
#include "../urbackupcommon/os_functions.h"
#include "database.h"
#include "dao/ServerFilesDao.h"
#include <assert.h>
#include "../Interface/Types.h"
#include "../Interface/File.h"
#include <memory>
#include "../Interface/Server.h"
#include "create_files_index.h"

MDB_env *LMDBFileIndex::env=NULL;
ISharedMutex* LMDBFileIndex::mutex=NULL;
LMDBFileIndex* LMDBFileIndex::fileindex=NULL;
THREADPOOL_TICKET LMDBFileIndex::fileindex_ticket = ILLEGAL_THREADPOOL_TICKET;


const size_t c_initial_map_size=1*1024*1024;
const size_t c_create_commit_n = 10000;


bool LMDBFileIndex::initFileIndex()
{
	mutex = Server->createSharedMutex();

	fileindex=new LMDBFileIndex;
	fileindex_ticket = Server->getThreadPool()->execute(fileindex, "fileindex writer");

	return !fileindex->has_error();
}


void LMDBFileIndex::shutdownFileIndex()
{
	fileindex->shutdown();
	Server->getThreadPool()->waitFor(fileindex_ticket);
}


LMDBFileIndex::LMDBFileIndex(bool no_sync)
	: _has_error(false), txn(NULL), map_size(c_initial_map_size), it_cursor(NULL), no_sync(no_sync)
{
	IScopedWriteLock lock(mutex);

	if(!create_env())
	{
		Server->Log("LMDB error creating env", LL_ERROR);
		_has_error=true;
	}
}

LMDBFileIndex::~LMDBFileIndex(void)
{
}


bool LMDBFileIndex::has_error(void)
{
	if ((Server->getFailBits() & IServer::FAIL_DATABASE_CORRUPTED) ||
		(Server->getFailBits() & IServer::FAIL_DATABASE_IOERR) ||
		(Server->getFailBits() & IServer::FAIL_DATABASE_FULL))
	{
		return false;
	}
	return _has_error;
}

void LMDBFileIndex::begin_txn(unsigned int flags)
{
	read_transaction_lock.reset(new IScopedReadLock(mutex));

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

void LMDBFileIndex::create(get_data_callback_t get_data_callback, void *userdata)
{
	begin_txn(0);

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES_NEW);

	ServerFilesDao filesdao(db);

	size_t n_done=0;
	size_t n_rows=0;

	SIndexKey last;
	int64 last_prev_entry;
	int64 last_id;
	db_results res;
	do
	{
		res=get_data_callback(n_done, n_rows, userdata);

		++n_rows;

		for(size_t i=0;i<res.size();++i)
		{
			const std::string& shahash=res[i]["shahash"];
			int64 id = watoi64(res[i]["id"]);
			SIndexKey key(reinterpret_cast<const char*>(shahash.c_str()), watoi64(res[i]["filesize"]), watoi(res[i]["clientid"]));

			int64 next_entry = watoi64(res[i]["next_entry"]);
			int64 prev_entry = watoi64(res[i]["prev_entry"]);
			int pointed_to = watoi(res[i]["pointed_to"]);

			assert(memcmp(&last, &key, sizeof(SIndexKey))!=1);

			if(key==last)
			{
				if(last_prev_entry==0)
				{
					filesdao.setPrevEntry(id, last_id);
				}

				if(next_entry==0
					&& (last_prev_entry==0 || last_prev_entry==id) )
				{
					filesdao.setNextEntry(last_id, id);
				}

				if(pointed_to)
				{
					filesdao.setPointedTo(0, id);
				}

				last=key;
				last_id=id;
				last_prev_entry=prev_entry;

				continue;
			}
			else
			{
				if(!pointed_to)
				{
					filesdao.setPointedTo(1, id);
				}
			}
			
			put(key, id, MDB_APPEND);

			if(_has_error)
			{
				Server->Log("LMDB error after putting element. Error state interrupting..", LL_ERROR);
				return;
			}

			if(n_done % 1000 == 0 && n_done>0)
			{
				if ((Server->getFailBits() & IServer::FAIL_DATABASE_CORRUPTED) ||
					(Server->getFailBits() & IServer::FAIL_DATABASE_IOERR) ||
					(Server->getFailBits() & IServer::FAIL_DATABASE_FULL))
				{
					Server->Log("Database error. Stopping.", LL_ERROR);
					return;
				}
				Server->Log("File entry index contains "+convert(n_done)+" entries now.", LL_INFO);
			}

			if(n_done % c_create_commit_n == 0 && n_done>0)
			{
				commit_transaction();
				begin_txn(0);
			}

			++n_done;

			last=key;
			last_id=id;
			last_prev_entry=prev_entry;
		}		
	}
	while(!res.empty());

	commit_transaction();
}

int64 LMDBFileIndex::get(const LMDBFileIndex::SIndexKey& key)
{
	begin_txn(MDB_RDONLY);

	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;

	int rc=mdb_get(txn, dbi, &mdb_tkey, &mdb_tvalue);

	int64 ret = 0;
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
		
		data.getVarInt(&ret);
	}

	abort_transaction();

	return ret;
}

void LMDBFileIndex::start_transaction(void)
{
	begin_txn(0);
}

void LMDBFileIndex::put_internal(const SIndexKey& key, int64 value, int flags, bool log, bool handle_enosp)
{
	CWData vdata;
	vdata.addVarInt(value);
			
	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;
	mdb_tvalue.mv_data=vdata.getDataPtr();
	mdb_tvalue.mv_size=vdata.getDataSize();

	int rc = mdb_put(txn, dbi, &mdb_tkey, &mdb_tvalue, flags);

	if(rc==MDB_MAP_FULL && handle_enosp)
	{
		mdb_txn_abort(txn);

		if(_has_error)
		{
			Server->Log("LMDB had error during increase (on put). Aborting...", LL_ERROR);
			start_transaction();
			return;
		}

		{
			read_transaction_lock.reset();

			IScopedWriteLock lock(mutex);

			destroy_env();

			map_size*=2;

			Server->Log("Increased LMDB database size to "+PrettyPrintBytes(map_size)+" (on put)", LL_DEBUG);

			if(!create_env())
			{
				Server->Log("Error creating env after database file size increase", LL_ERROR);
				_has_error=true;
				start_transaction();
				return;
			}
			
		}

		start_transaction();

		replay_transaction_log();
		
		put_internal(key, value, flags, false, true);
	}
	else if(rc==MDB_BAD_TXN && handle_enosp)
	{
		mdb_txn_abort(txn);
		
		if(_has_error)
		{
			Server->Log("LMDB had error on BAD_TXN (on put). Aborting...", LL_ERROR);
			start_transaction();
			return;
		}
		
		start_transaction();
		
		replay_transaction_log();
		
		put_internal(key, value, flags, false, false);
	}
	else if(rc)
	{
		Server->Log("LMDB: Failed to put data ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}

	if(!_has_error && log)
	{
		STransactionLogItem item = { key, value, flags};
		transaction_log.push_back(item);
	}
}

void LMDBFileIndex::put( const SIndexKey& key, int64 value )
{
	put(key, value, 0);
}

void LMDBFileIndex::put( const SIndexKey& key, int64 value, int flags )
{
	put_internal(key, value, flags, true, true);
}

void LMDBFileIndex::del_internal(const SIndexKey& key, bool log, bool handle_enosp)
{		
	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	int rc = mdb_del(txn, dbi, &mdb_tkey, NULL);

	if(rc==MDB_MAP_FULL && handle_enosp)
	{
		mdb_txn_abort(txn);

		if(_has_error)
		{
			Server->Log("LMDB had error during increase (on del). Aborting...", LL_ERROR);
			start_transaction();
			return;
		}

		{
			read_transaction_lock.reset();

			IScopedWriteLock lock(mutex);

			destroy_env();

			map_size*=2;

			Server->Log("Increased LMDB database size to "+PrettyPrintBytes(map_size)+" (on delete)", LL_DEBUG);

			if(!create_env())
			{
				Server->Log("Error creating env after database file size increase", LL_ERROR);
				_has_error=true;
				start_transaction();
				return;
			}
			
		}

		start_transaction();

		replay_transaction_log();

		del(key);
	}
	else if(rc==MDB_BAD_TXN && handle_enosp)
	{
		mdb_txn_abort(txn);
		
		if(_has_error)
		{
			Server->Log("LMDB had error on BAD_TXN (on del). Aborting...", LL_ERROR);
			start_transaction();
			return;
		}
		
		start_transaction();
		
		replay_transaction_log();
		
		del_internal(key, true, false);
	}
	else if (rc == MDB_NOTFOUND)
	{
		FILEENTRY_DEBUG(Server->Log("LMDB: Failed to delete data (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR));
	}
	else if(rc)
	{
		Server->Log("LMDB: Failed to delete data ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}

	if(log)
	{
		STransactionLogItem item = { key, 0, 0 };
		transaction_log.push_back(item);
	}	
}

void LMDBFileIndex::commit_transaction(void)
{
	commit_transaction_internal(true);
}

void LMDBFileIndex::commit_transaction_internal(bool handle_enosp)
{
	int rc = mdb_txn_commit(txn);
	
	
	if(rc==MDB_MAP_FULL && handle_enosp)
	{
		if(_has_error)
		{
			Server->Log("LMDB had error during increase (on commit). Aborting...", LL_ERROR);
			start_transaction();
			return;
		}

		{
			read_transaction_lock.reset();

			IScopedWriteLock lock(mutex);

			destroy_env();

			map_size*=2;

			Server->Log("Increased LMDB database size to "+PrettyPrintBytes(map_size)+" (on commit)", LL_DEBUG);

			if(!create_env())
			{
				Server->Log("Error creating env after database file size increase", LL_ERROR);
				_has_error=true;
				start_transaction();
				return;
			}
			
		}

		start_transaction();

		replay_transaction_log();
		
		commit_transaction_internal(false);
	}
	else if(rc==MDB_BAD_TXN && handle_enosp)
	{
		if(_has_error)
		{
			Server->Log("LMDB had error on BAD_TXN (on commit). Aborting...", LL_ERROR);
			start_transaction();
			return;
		}
		
		start_transaction();
		
		replay_transaction_log();
		
		commit_transaction_internal(false);
	}
	else if(rc)
	{
		Server->Log("LMDB: Failed to commit transaction ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}

	read_transaction_lock.reset();
	transaction_log.clear();
}

bool LMDBFileIndex::create_env()
{
	int rc;
	if(env==NULL)
	{
		rc = mdb_env_create(&env);
		if(rc)
		{
			Server->Log("LMDB: Failed to create LMDB env ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			return false;
		}

		{
			std::auto_ptr<IFile> lmdb_f(Server->openFile("urbackup/fileindex/backup_server_files_index.lmdb", MODE_READ));
			if(lmdb_f.get()!=NULL)
			{
				while(lmdb_f->Size()>static_cast<_i64>(map_size))
				{
					map_size*=2;
				}
			}
		}
		
		rc = mdb_env_set_maxreaders(env, 4094);

		if (rc)
		{
			Server->Log("LMDB: Failed to set max readers (" + (std::string)mdb_strerror(rc) + ")", LL_ERROR);
			return false;
		}

		rc = mdb_env_set_mapsize(env, map_size);

		if(rc)
		{
			Server->Log("LMDB: Failed to set map size ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			return false;
		}

		os_create_dir("urbackup/fileindex");

		unsigned int flags = MDB_NOSUBDIR|MDB_NOMETASYNC;
		if(no_sync)
		{
			flags|=MDB_NOSYNC;
		}
		rc = mdb_env_open(env, "urbackup/fileindex/backup_server_files_index.lmdb", flags, 0664);

		if(rc)
		{
			Server->Log("LMDB: Failed to open LMDB database file ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			return false;
		}

		return true;
	}
	else
	{
		return true;
	}
}

void LMDBFileIndex::destroy_env()
{
	mdb_env_close(env);
	env=NULL;
}

size_t LMDBFileIndex::get_map_size()
{
	return map_size;
}

int64 LMDBFileIndex::get_any_client( const SIndexKey& key )
{
	begin_txn(MDB_RDONLY);

	MDB_cursor* cursor;

	mdb_cursor_open(txn, dbi, &cursor);

	SIndexKey orig_key = key;

	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;

	int rc=mdb_cursor_get(cursor,&mdb_tkey, &mdb_tvalue, MDB_SET_RANGE);

	SIndexKey* curr_key = reinterpret_cast<SIndexKey*>(mdb_tkey.mv_data);

	int64 ret = 0;
	if(rc==MDB_NOTFOUND ||
		!orig_key.isEqualWithoutClientid(orig_key) )
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

		data.getVarInt(&ret);
	}

	mdb_cursor_close(cursor);

	abort_transaction();

	return ret;
}

void LMDBFileIndex::abort_transaction()
{
	mdb_txn_abort(txn);

	read_transaction_lock.reset();
	transaction_log.clear();
}

std::map<int, int64> LMDBFileIndex::get_all_clients( const SIndexKey& key )
{
	begin_txn(MDB_RDONLY);

	MDB_cursor* cursor;

	mdb_cursor_open(txn, dbi, &cursor);

	SIndexKey orig_key = key;

	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;

	int rc=mdb_cursor_get(cursor,&mdb_tkey, &mdb_tvalue, MDB_SET_RANGE);

	std::map<int, int64> ret;

	SIndexKey* curr_key = reinterpret_cast<SIndexKey*>(mdb_tkey.mv_data);

	while(rc==0 &&
		orig_key.isEqualWithoutClientid(*curr_key))
	{
		CRData data((const char*)mdb_tvalue.mv_data, mdb_tvalue.mv_size);
		int64 entryid;
		data.getVarInt(&entryid);

		ret[curr_key->getClientid()] = entryid;

		rc=mdb_cursor_get(cursor, &mdb_tkey, &mdb_tvalue, MDB_NEXT);
		curr_key = reinterpret_cast<SIndexKey*>(mdb_tkey.mv_data);
	}


	if(rc && rc!=MDB_NOTFOUND)
	{
		Server->Log("LMDB: Failed to read ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		_has_error=true;
	}

	mdb_cursor_close(cursor);

	abort_transaction();

	return ret;
}

int64 LMDBFileIndex::get_prefer_client( const SIndexKey& key )
{
	begin_txn(MDB_RDONLY);

	MDB_cursor* cursor;

	mdb_cursor_open(txn, dbi, &cursor);

	SIndexKey orig_key = key;

	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;

	int rc=mdb_cursor_get(cursor,&mdb_tkey, &mdb_tvalue, MDB_SET_RANGE);

	int64 ret = 0;
	int retry_prev=2;
	while(rc==0 && retry_prev>0 && !_has_error)
	{
		SIndexKey* curr_key = reinterpret_cast<SIndexKey*>(mdb_tkey.mv_data);

		if(rc==MDB_NOTFOUND)
		{
			retry_prev=0;
		}
		else if(rc)
		{
			Server->Log("LMDB: Failed to read ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			_has_error=true;
		}
		else if( curr_key->isEqualWithoutClientid(orig_key))
		{
			CRData data((const char*)mdb_tvalue.mv_data, mdb_tvalue.mv_size);
			data.getVarInt(&ret);
			retry_prev=0;
		}
		else
		{
			rc=mdb_cursor_get(cursor, &mdb_tkey, &mdb_tvalue, MDB_PREV);
			--retry_prev;
		}
	}

	mdb_cursor_close(cursor);

	abort_transaction();

	return ret;
}

void LMDBFileIndex::replay_transaction_log()
{
	for(size_t i=0;i<transaction_log.size();++i)
	{
		if(transaction_log[i].value!=0)
		{
			put_internal(transaction_log[i].key, transaction_log[i].value, transaction_log[i].flags, false, false);
		}
		else
		{
			del_internal(transaction_log[i].key, false, false);
		}
	}
}

void LMDBFileIndex::start_iteration()
{
	SIndexKey key;
	
	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;

	mdb_cursor_open(txn, dbi, &it_cursor);

	int rc=mdb_cursor_get(it_cursor, &mdb_tkey, &mdb_tvalue, MDB_SET_RANGE);

	if(rc)
	{
		_has_error=true;
		Server->Log("LMDB: Failed to read ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		return;
	}
}

std::map<int, int64> LMDBFileIndex::get_next_entries_iteration(bool& has_next)
{
	SIndexKey key;

	MDB_val mdb_tkey;
	mdb_tkey.mv_data=const_cast<void*>(static_cast<const void*>(&key));
	mdb_tkey.mv_size=sizeof(SIndexKey);

	MDB_val mdb_tvalue;

	SIndexKey* start_key;

	int rc = mdb_cursor_get(it_cursor, &mdb_tkey, &mdb_tvalue, MDB_GET_CURRENT);

	if(rc && rc!=MDB_NOTFOUND)
	{
		_has_error=true;
		Server->Log("LMDB: Failed to read ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
		has_next=false;
		return std::map<int, int64>();
	}
	else if(rc==MDB_NOTFOUND)
	{
		has_next=false;
		return std::map<int, int64>();
	}

	start_key = reinterpret_cast<SIndexKey*>(mdb_tkey.mv_data);

	std::map<int, int64> ret;

	{
		int64 entryid;
		CRData data((const char*)mdb_tvalue.mv_data, mdb_tvalue.mv_size);
		data.getVarInt(&entryid);

		ret[start_key->getClientid()]=entryid;
	}
	

	do 
	{
		SIndexKey* key_curr;
		rc = mdb_cursor_get(it_cursor, &mdb_tkey, &mdb_tvalue, MDB_NEXT);

		if(rc && rc!=MDB_NOTFOUND)
		{
			_has_error=true;
			Server->Log("LMDB: Failed to read ("+(std::string)mdb_strerror(rc)+")", LL_ERROR);
			has_next=false;
			return std::map<int, int64>();
		}
		else if(rc==MDB_NOTFOUND)
		{
			has_next=false;
			return ret;
		}

		key_curr = reinterpret_cast<SIndexKey*>(mdb_tkey.mv_data);

		if(!start_key->isEqualWithoutClientid(*key_curr))
		{
			return ret;
		}

		int64 entryid;
		CRData data((const char*)mdb_tvalue.mv_data, mdb_tvalue.mv_size);
		data.getVarInt(&entryid);

		ret[key.getClientid()]=entryid;

	} while (true);
}

void LMDBFileIndex::stop_iteration()
{
	mdb_cursor_close(it_cursor);
}

void LMDBFileIndex::del( const SIndexKey& key )
{
	del_internal(key, true, true);
}
