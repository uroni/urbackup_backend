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
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/ThreadPool.h"
#include "database.h"
#include "../Interface/Database.h"
#include <memory>

namespace
{
	class TimedDbLock : public IThread
	{
	public:
		TimedDbLock()
			: mutex(Server->createMutex()), cond(Server->createCondition()), has_lock(false)
		{

		}

		void operator()()
		{
			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);
			db->lockForSingleUse();
			{
				IScopedLock lock(mutex.get());
				has_lock = true;
				cond->notify_all();
			}			
			Server->wait(1000);
			db->unlockForSingleUse();
		}

		void waitForLock()
		{
			IScopedLock lock(mutex.get());
			while (!has_lock)
			{
				cond->wait(&lock);
			}
		}

	private:
		std::auto_ptr<IMutex> mutex;
		std::auto_ptr<ICondition> cond;
		bool has_lock;
	};
}

WalCheckpointThread::WalCheckpointThread()
	: last_checkpoint_wal_size(0)
{
}

void WalCheckpointThread::checkpoint()
{
	int mode = MODE_READ;
#ifdef _WIN32
	mode=MODE_READ_DEVICE;
#endif
	std::auto_ptr<IFile> wal_file(Server->openFile("urbackup" + os_file_sep() + "backup_server_files.db-wal", mode));

	if(wal_file.get()!=NULL)
	{
		int64 wal_size = wal_file->Size();
		if (wal_size > 1 * 1024 * 1024 * 1024) //>1GiB
		{
			wal_file.reset();

			Server->Log("Files WAL file greater than 1GiB. Doing full WAL checkpoint...", LL_INFO);

			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);

			//Get rid of writers for a second
			TimedDbLock timed_db_lock;
			THREADPOOL_TICKET ticket = Server->getThreadPool()->execute(&timed_db_lock);
			timed_db_lock.waitForLock();

			db->Write("PRAGMA wal_checkpoint(PASSIVE)");

			Server->getThreadPool()->waitFor(ticket);

			db->lockForSingleUse();
			db->Write("PRAGMA wal_checkpoint(TRUNCATE)");
			db->unlockForSingleUse();

			last_checkpoint_wal_size = 0;
		}
		else if (wal_size - last_checkpoint_wal_size > 100 * 1024 * 1024) //100MB
		{
			last_checkpoint_wal_size = wal_size;

			wal_file.reset();

			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);

			//Get rid of writers for a second
			TimedDbLock timed_db_lock;
			THREADPOOL_TICKET ticket = Server->getThreadPool()->execute(&timed_db_lock);
			timed_db_lock.waitForLock();

			db->Write("PRAGMA wal_checkpoint(PASSIVE)");

			Server->getThreadPool()->waitFor(ticket);
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
