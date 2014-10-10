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
		db->Write("PRAGMA cache_size = -"+nconvert(server_settings.getSettings()->update_stats_cachesize));
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
		int64 id = watoi64(res[L"id"]);
		int64 filesize = watoi64(res[L"filesize"]);
		int clientid = watoi(res[L"clientid"]);
		bool found_entry=false;

		int64 entryid = fileindex->get_with_cache_exact(FileIndex::SIndexKey(reinterpret_cast<const char*>(res[L"shahash"].data()),
			filesize, clientid));

		if(entryid==0)
		{
			Server->Log(L"Cannot find entry for file with id "+convert(id)+L" with path \""+res[L"fullpath"]+L"\"", LL_ERROR);
			has_error=true;
			continue;
		}		

		bool first=true;

		int64 backward_entryid=0;
		int64 prev_entryid=0;
		while(entryid!=0)
		{
			ServerBackupDao::SFindFileEntry fileentry = backupdao.getFileEntry(entryid);
			
			//Server->Log("Current entry id="+nconvert(fileentry.id));

			if(fileentry.id == id)
			{
				found_entry=true;
			}

			if(first)
			{
				if(!fileentry.pointed_to)
				{
					Server->Log(L"First entry with id "+convert(entryid)+L" does not have pointed_to set to a value not equal 0", LL_ERROR);
					has_error=true;
				}	
				backward_entryid=fileentry.prev_entry;
				first=false;
			}

			if(!fileentry.exists)
			{
				Server->Log(L"File entry for file with id "+convert(entryid)+L" in index does not exist in database", LL_ERROR);
				has_error=true;
				break;
			}

			if(prev_entryid!=0 &&
				fileentry.next_entry!=prev_entryid)
			{
				Server->Log(L"Next entry for file with id "+convert(entryid)+L" is wrong. Assumed="+convert(prev_entryid)+L" Actual="+convert(fileentry.next_entry)+L" Origin="+convert(id), LL_ERROR);
				has_error=true;
				break;
			}

			if(fileentry.shahash!=res[L"shahash"])
			{
				Server->Log(L"Shahash of entry with id "+convert(entryid)+L" differs from shahash of entry with id "+convert(id)+L". It should not differ.", LL_ERROR);
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
					Server->Log(L"File entry for file with id "+convert(entryid)+L" in index does not exist in database", LL_ERROR);
					has_error=true;
					break;
				}

				if(prev_entryid!=0 &&
					fileentry.prev_entry!=prev_entryid)
				{
					Server->Log(L"Previous entry for file with id "+convert(entryid)+L" is wrong. Assumed="+convert(prev_entryid)+L" Actual="+convert(fileentry.prev_entry)+L" Origin="+convert(id), LL_ERROR);
					has_error=true;
					break;
				}

				if(fileentry.shahash!=res[L"shahash"])
				{
					Server->Log(L"Shahash of entry with id "+convert(entryid)+L" differs from shahash of entry with id "+convert(id)+L". It should not differ. -2", LL_ERROR);
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
			Server->Log(L"Entry with id "+convert(id)+L" is not in the list and therefore not indexed by the file entry index.", LL_ERROR);
			has_error=true;
		}

		++n_checked;

		if(n_checked%100==0)
		{
			Server->Log("Checked "+nconvert(n_checked)+" file entries", LL_INFO);
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

