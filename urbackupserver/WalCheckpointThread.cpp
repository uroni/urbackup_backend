#include "WalCheckpointThread.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/File.h"
#include "../Interface/Server.h"
#include "database.h"
#include "../Interface/Database.h"
#include <memory>

void WalCheckpointThread::checkpoint(bool in_transaction)
{
	std::auto_ptr<IFile> wal_file(Server->openFile(L"urbackup" + os_file_sep() + L"backup_server.db-wal"));

	if(wal_file.get()!=NULL
		&& wal_file->Size()>1*1024*1024*1024) //>1GiB
	{
		wal_file.reset();

		Server->Log("WAL file greater than 1GiB. Doing WAL checkpoint...", LL_INFO);

		IDatabase* db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);


		if(in_transaction)
		{
			db->Write("BEGIN EXCLUSIVE");
		}

		db->Write("PRAGMA wal_checkpoint(RESTART)");
		
		if(in_transaction)
		{
			db->EndTransaction();
		}

		checkpoint(true);
	}
	else
	{
		Server->Log("Could not open WAL file (checking for WAL file size)", LL_DEBUG);
	}
}

void WalCheckpointThread::operator()()
{
	while(true)
	{
		Server->wait(1*60*1000); //every min
		checkpoint();
	}
}
