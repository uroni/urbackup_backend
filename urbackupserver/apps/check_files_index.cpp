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

#include "app.h"
#include "../../stringtools.h"
#include "../../Interface/DatabaseCursor.h"
#include <memory>
#include "../FileIndex.h"
#include "../create_files_index.h"
#include "../dao/ServerBackupDao.h"
#include "../server_settings.h"


void open_settings_database(bool use_berkeleydb);

int check_files_index()
{
	bool use_berkeleydb;
	open_server_database(use_berkeleydb, true);
	open_settings_database(use_berkeleydb);

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if(db==NULL)
	{
		Server->Log("Could not open main database", LL_ERROR);
		return 1;
	}

	if(db->getEngineName()=="sqlite")
	{
		ServerSettings server_settings(db);
		db->Write("PRAGMA cache_size = -"+convert(server_settings.getSettings()->update_stats_cachesize));
	}


	IQuery* q_iterate;
	
	if(Server->getServerParameter("check_last").empty())
	{
		q_iterate = db->Prepare("SELECT id, shahash, filesize, clientid, fullpath FROM files");
	}
	else
	{
		q_iterate = db->Prepare("SELECT id, shahash, filesize, clientid, fullpath FROM files ORDER BY id DESC LIMIT "+Server->getServerParameter("check_last"));
	}

	IDatabaseCursor* cursor = q_iterate->Cursor();

	std::auto_ptr<FileIndex> fileindex(create_lmdb_files_index());

	if(!fileindex.get())
	{
		Server->Log("Fileindex not present", LL_ERROR);
		return 2;
	}

	ServerBackupDao backupdao(db);

	int64 n_checked = 0;

	bool has_error=false;

	db_single_result res;
	while(cursor->next(res))
	{
		int64 id = watoi64(res["id"]);
		int64 filesize = watoi64(res["filesize"]);
		int clientid = watoi(res["clientid"]);
		bool found_entry=false;

		int64 entryid = fileindex->get_with_cache_exact(FileIndex::SIndexKey(reinterpret_cast<const char*>(res["shahash"].data()),
			filesize, clientid));

		if(entryid==0)
		{
			Server->Log("Cannot find entry for file with id "+convert(id)+" with path \""+res["fullpath"]+"\"", LL_ERROR);
			has_error=true;
			continue;
		}		
		
		int64 found_entryid=entryid;

		bool first=true;

		int64 backward_entryid=0;
		int64 prev_entryid=0;
		while(entryid!=0)
		{
			ServerBackupDao::SFindFileEntry fileentry = backupdao.getFileEntry(entryid);
			
			//Server->Log("Current entry id="+convert(fileentry.id));

			if(fileentry.id == id)
			{
				found_entry=true;
			}

			if(clientid!=fileentry.clientid)
			{
				Server->Log("First entry with id "+convert(entryid)+" has wrong clientid (expected: "+convert(clientid)+" has: "+convert(fileentry.clientid)+")", LL_ERROR);
				has_error=true;
			}

			if(first)
			{
				if(!fileentry.pointed_to)
				{
					Server->Log("First entry with id "+convert(entryid)+" does not have pointed_to set to a value unequal 0 ("+convert(fileentry.pointed_to)+")", LL_ERROR);
					has_error=true;
				}	
				backward_entryid=fileentry.next_entry;
				first=false;
			}

			if(!fileentry.exists)
			{
				Server->Log("File entry for file with id "+convert(entryid)+" in index does not exist in database", LL_ERROR);
				has_error=true;
				break;
			}

			if(prev_entryid!=0 &&
				fileentry.next_entry!=prev_entryid)
			{
				Server->Log("Next entry for file with id "+convert(entryid)+" is wrong. Assumed="+convert(prev_entryid)+" Actual="+convert(fileentry.next_entry)+" Origin="+convert(id), LL_ERROR);
				has_error=true;
				break;
			}

			if(fileentry.shahash!=res["shahash"])
			{
				Server->Log("Shahash of entry with id "+convert(entryid)+" differs from shahash of entry with id "+convert(id)+". It should not differ.", LL_ERROR);
				has_error=true;
				break;
			}

			prev_entryid = entryid;

			entryid = fileentry.prev_entry;

			if(entryid==0 || prev_entryid==id)
			{
				break;
			}
		}

		if(!found_entry)
		{
			entryid = backward_entryid;
			prev_entryid = 0;
			while(entryid!=0)
			{
				ServerBackupDao::SFindFileEntry fileentry = backupdao.getFileEntry(entryid);

				if(fileentry.id == id)
				{
					found_entry=true;
				}

				if(!fileentry.exists)
				{
					Server->Log("File entry for file with id "+convert(entryid)+" in index does not exist in database", LL_ERROR);
					has_error=true;
					break;
				}

				if(prev_entryid!=0 &&
					fileentry.prev_entry!=prev_entryid)
				{
					Server->Log("Previous entry for file with id "+convert(entryid)+" is wrong. Assumed="+convert(prev_entryid)+" Actual="+convert(fileentry.prev_entry)+" Origin="+convert(id), LL_ERROR);
					has_error=true;
					break;
				}

				if(fileentry.shahash!=res["shahash"])
				{
					Server->Log("Shahash of entry with id "+convert(entryid)+" differs from shahash of entry with id "+convert(id)+". It should not differ. -2", LL_ERROR);
					has_error=true;
					break;
				}

				prev_entryid = entryid;

				entryid = fileentry.next_entry;

				if(entryid==0 || prev_entryid==id)
				{
					break;
				}
			}
		}

		if(!found_entry)
		{
			Server->Log("Entry with id "+convert(id)+" is not in the list and therefore not indexed by the file entry index. Initial list id is "+convert(found_entryid), LL_ERROR);
			has_error=true;
		}

		++n_checked;

		if(n_checked%10000==0)
		{
			Server->Log("Checked "+convert(n_checked)+" file entries", LL_INFO);
		}
	}

	Server->Log("Check complete");

	if(has_error)
	{
		Server->Log("There were errors.", LL_ERROR);
		return 1;
	}
	
	return 0;
}

