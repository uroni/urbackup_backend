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

#include "WalCheckpointThread.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/File.h"
#include "../Interface/Server.h"
#include "database.h"
#include "../Interface/Database.h"
#include "../stringtools.h"
#include <memory>

IMutex* WalCheckpointThread::mutex = NULL;
ICondition* WalCheckpointThread::cond = NULL;
std::set<std::string> WalCheckpointThread::locked_dbs;
std::set<std::string> WalCheckpointThread::tolock_dbs;

WalCheckpointThread::WalCheckpointThread(int64 passive_checkpoint_size, int64 full_checkpoint_size, const std::string& db_fn, DATABASE_ID db_id, std::string db_name)
	: last_checkpoint_wal_size(0), passive_checkpoint_size(passive_checkpoint_size),
	full_checkpoint_size(full_checkpoint_size), db_fn(db_fn), db_id(db_id), cannot_open(false), db_name(db_name)
{
}

void WalCheckpointThread::checkpoint()
{
	int mode = MODE_READ;
#ifdef _WIN32
	mode=MODE_READ_DEVICE;
#endif
	std::auto_ptr<IFile> wal_file(Server->openFile(db_fn+"-wal", mode));

	if(wal_file.get()!=NULL)
	{
		cannot_open = false;

		int64 wal_size = wal_file->Size();
		if (wal_size > full_checkpoint_size)
		{
			wal_file.reset();

			passive_checkpoint();

			sync_database();

			Server->Log("Files WAL file "+ db_fn + "-wal greater than "+PrettyPrintBytes(full_checkpoint_size)+". Doing full WAL checkpoint...", LL_INFO);

			IDatabase* db = Server->getDatabase(Server->getThreadID(), db_id);
			db->lockForSingleUse();
			if (db_name.empty())
			{
				db->Write("PRAGMA wal_checkpoint(TRUNCATE)");
			}
			else
			{
				db->Write("PRAGMA " + db_name + ".wal_checkpoint(TRUNCATE)");
			}
			db->unlockForSingleUse();

			Server->Log("Full checkpoint of "+ db_fn + "-wal done.", LL_INFO);

			last_checkpoint_wal_size = 0;
		}
		else if (wal_size - last_checkpoint_wal_size > passive_checkpoint_size)
		{
			last_checkpoint_wal_size = wal_size;

			wal_file.reset();

			passive_checkpoint();
		}
		else if (wal_size < last_checkpoint_wal_size)
		{
			last_checkpoint_wal_size = 0;
		}
	}
	else
	{
		if (!cannot_open)
		{
			Server->Log("Could not open WAL file " + db_fn + "-wal" + " (wal checkpoint thread)", LL_DEBUG);
		}
		cannot_open = true;
	}
}

void WalCheckpointThread::operator()()
{
	while(true)
	{
		waitAndLockForBackup();
		checkpoint();
	}
}

void WalCheckpointThread::lockForBackup(const std::string & fn)
{
	IScopedLock lock(mutex);
	tolock_dbs.insert(fn);
	cond->notify_all();

	while (locked_dbs.find(fn) == locked_dbs.end())
	{
		lock.relock(NULL);
		Server->wait(1000);
		lock.relock(mutex);
	}
}

void WalCheckpointThread::unlockForBackup(const std::string & fn)
{
	IScopedLock lock(mutex);
	std::set<std::string>::iterator it = tolock_dbs.find(fn);
	if (it != tolock_dbs.end())
	{
		tolock_dbs.erase(it);
	}
	cond->notify_all();
}

void WalCheckpointThread::init_mutex()
{
	mutex = Server->createMutex();
	cond = Server->createCondition();
}

void WalCheckpointThread::destroy_mutex()
{
	Server->destroy(mutex);
	Server->destroy(cond);
}

void WalCheckpointThread::waitAndLockForBackup()
{
	IScopedLock lock(mutex);

	cond->wait(&lock, 10000);

	bool locked_db = false;
	while (tolock_dbs.find(db_fn)!= tolock_dbs.end())
	{
		if (!locked_db)
		{
			locked_db = true;
			locked_dbs.insert(db_fn);
		}

		cond->wait(&lock);
	}

	if (locked_db)
	{
		locked_dbs.erase(locked_dbs.find(db_fn));
	}
}

void WalCheckpointThread::sync_database()
{
	ScopedBackgroundPrio background_prio;

	Server->Log("Syncing database " + db_fn + "...", LL_DEBUG);

	int rw_mode = MODE_RW;
#ifdef _WIN32
	rw_mode = MODE_RW_DEVICE;
#endif

	if (db_file.get() == NULL)
	{
		db_file.reset(Server->openFile(db_fn, rw_mode));
	}
	if (db_file.get() != NULL)
	{
		db_file->Sync();
	}

	Server->Log("Syncing wal file " + db_fn + "-wal...", LL_DEBUG);

	{
		std::auto_ptr<IFile> rw_wal_file(Server->openFile(db_fn + "-wal", rw_mode));
		if (rw_wal_file.get() != NULL)
		{
			rw_wal_file->Sync();
		}
	}
}

void WalCheckpointThread::passive_checkpoint()
{
	ScopedBackgroundPrio background_prio;

	Server->Log("Starting passive WAL checkpoint of "+db_fn+"...", LL_DEBUG);
	IDatabase* db = Server->getDatabase(Server->getThreadID(), db_id);
	db_results res = db->Read("PRAGMA wal_checkpoint(PASSIVE)");
	if (!res.empty())
	{
		Server->Log("Passive WAL checkpoint of " + db_fn + " completed busy=" + res[0]["busy"] + " checkpointed=" + res[0]["checkpointed"] + " log=" + res[0]["log"], LL_DEBUG);
	}
}
