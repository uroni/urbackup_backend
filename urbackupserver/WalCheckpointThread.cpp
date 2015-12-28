/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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
#include <memory>

void WalCheckpointThread::checkpoint()
{
	int mode = MODE_READ;
#ifdef _WIN32
	mode=MODE_READ_DEVICE;
#endif
	std::auto_ptr<IFile> wal_file(Server->openFile("urbackup" + os_file_sep() + "backup_server.db-wal", mode));

	if(wal_file.get()!=NULL)
	{
		if(wal_file->Size()>1*1024*1024*1024) //>1GiB
		{
			wal_file.reset();

			Server->Log("WAL file greater than 1GiB. Doing WAL checkpoint...", LL_INFO);

			IDatabase* db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

			db->lockForSingleUse();
			db->Write("PRAGMA wal_checkpoint(TRUNCATE)");
			db->unlockForSingleUse();
		}		
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
