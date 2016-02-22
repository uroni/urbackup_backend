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

#ifndef CLIENT_ONLY

#include "action_header.h"
#include "../server_cleanup.h"
#include "../../Interface/ThreadPool.h"
#include "../create_files_index.h"
#include "../dao/ServerFilesDao.h"
#include "../database.h"

namespace 
{
	class RecalculateStatistics : public IThread
	{
	public:
		RecalculateStatistics()
		{
		}

		void operator()(void)
		{
			IDatabase* files_db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_FILES);
			ServerFilesDao filesdao(files_db);
			
			std::auto_ptr<FileIndex> fileindex(create_lmdb_files_index());

			fileindex->start_transaction();
			fileindex->start_iteration();

			std::map<int, int64> client_sizes;
			std::map<int, int64> entries;
			bool has_next=true;
			do 
			{
				entries = fileindex->get_next_entries_iteration(has_next);

				if(!entries.empty())
				{
					ServerFilesDao::SStatFileEntry fentry = filesdao.getStatFileEntry(entries.begin()->second);

					if(fentry.exists)
					{
						int64 size_per_client = fentry.filesize;
						size_per_client/=entries.size();


						for(std::map<int, int64>::iterator it=entries.begin();it!=entries.end();++it)
						{
							client_sizes[it->first]+=size_per_client;
						}
					}
				}

			} while (has_next);

			fileindex->stop_iteration();
			fileindex->commit_transaction();


			IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

			ServerBackupDao backupdao(db);

			db->BeginWriteTransaction();

			db->Write("UPDATE clients SET bytes_used_files=0");

			for(std::map<int, int64>::iterator it=client_sizes.begin();
				it!=client_sizes.end();++it)
			{
				backupdao.setClientUsedFilebackupSize(it->second, it->first);
			}

			db->EndTransaction();

			ServerCleanupThread::updateStats(false);
			delete this;
		}
	};
}

ACTION_IMPL(usage)
{
	Helper helper(tid, &POST, &PARAMS);

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL )
	{
		IDatabase *db=helper.getDatabase();
		if(helper.getRights("piegraph")=="all")
		{
			IQuery *q=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used, bytes_used_files, bytes_used_images, name FROM clients ORDER BY (bytes_used_files+bytes_used_images) DESC");
			db_results res=q->Read();
		
			JSON::Array usage;
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object obj;
				obj.set("used",atof(res[i]["used"].c_str()));
				obj.set("files",atof(res[i]["bytes_used_files"].c_str()));
				obj.set("images",atof(res[i]["bytes_used_images"].c_str()));
				obj.set("name",res[i]["name"]);
				usage.add(obj);
			}
			ret.set("usage", usage);
		}
		if(helper.getRights("reset_statistics")=="all")
		{
			ret.set("reset_statistics", "true");

			if(POST["recalculate"]=="true")
			{
				Server->getThreadPool()->execute(new RecalculateStatistics, "statistics recalculation");
			}
		}
	}
	else
	{
		ret.set("error", 1);
	}
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
