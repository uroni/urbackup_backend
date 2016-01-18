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

WalCheckpointThread::WalCheckpointThread(int64 passive_checkpoint_size, int64 full_checkpoint_size, const std::string& db_fn, DATABASE_ID db_id)
	: last_checkpoint_wal_size(0), passive_checkpoint_size(passive_checkpoint_size),
	full_checkpoint_size(full_checkpoint_size), db_fn(db_fn), db_id(db_id)
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
		int64 wal_size = wal_file->Size();
		if (wal_size > full_checkpoint_size)
		{
			wal_file.reset();

			passive_checkpoint();

			Server->Log("Files WAL file "+ db_fn + "-wal greater than "+PrettyPrintBytes(full_checkpoint_size)+". Doing full WAL checkpoint...", LL_INFO);

			IDatabase* db = Server->getDatabase(Server->getThreadID(), db_id);
			db->lockForSingleUse();
			db->Write("PRAGMA wal_checkpoint(TRUNCATE)");
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
		Server->Log("Could not open WAL file (wal checkpoint thread)", LL_WARNING);
	}
}

void WalCheckpointThread::operator()()
{
	while(true)
	{
		Server->wait(10000); //every 10s
		checkpoint();
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

	{
		std::auto_ptr<IFile> db_file(Server->openFile(db_fn, rw_mode));
		if (db_file.get() != NULL)
		{
			db_file->Sync();
		}
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
