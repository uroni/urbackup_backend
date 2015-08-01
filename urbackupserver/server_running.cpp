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

#ifndef CLIENT_ONLY

#include "server_running.h"
#include "../Interface/Database.h"
#include "../Interface/Server.h"
#include "database.h"

ServerRunningUpdater::ServerRunningUpdater(int pBackupid, bool pImage) : backupid(pBackupid), image(pImage)
{
	do_stop=false;
	suspended=false;
	mutex=Server->createMutex();
	cond=Server->createCondition();
}

ServerRunningUpdater::~ServerRunningUpdater()
{
	Server->destroy(mutex);
	Server->destroy(cond);
}

void ServerRunningUpdater::operator()(void)
{
	IQuery *q;
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
	    Server->Log("Error: Could not find database in ServerRunningUpdater", LL_ERROR);
	    return;
	}
	if(image)
	{
		q=db->Prepare("UPDATE backup_images SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	}
	else
	{
		q=db->Prepare("UPDATE backups SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	}

	while(do_stop==false)
	{
		IScopedLock lock(mutex);
		cond->wait(&lock, 60000);
		if(do_stop==false && suspended==false)
		{
			q->Bind(backupid);
			q->Write();
			q->Reset();
		}
	}

	db->destroyQuery(q);
	db->freeMemory();
	delete this;
}

void ServerRunningUpdater::stop(void)
{
	IScopedLock lock(mutex);
	cond->notify_all();
	do_stop=true;
}

void ServerRunningUpdater::suspend(bool b)
{
	suspended=b;
}

#endif //CLIENT_ONLY
