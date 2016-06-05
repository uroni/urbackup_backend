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

#include "server_dir_links.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "server_settings.h"
#include "../Interface/Mutex.h"
#include "../Interface/Database.h"
#include "../Interface/File.h"
#include "database.h"

namespace
{
	void reference_all_sublinks(ServerLinkDao& link_dao, int clientid, const std::string& target, const std::string& new_target)
	{
		std::string escaped_target = escape_glob_sql(target);

		std::vector<ServerLinkDao::DirectoryLinkEntry> entries = link_dao.getLinksInDirectory(clientid, escaped_target+os_file_sep()+"*");

		for(size_t i=0;i<entries.size();++i)
		{
			std::string subpath = entries[i].target.substr(target.size());
			std::string new_link_path = new_target + subpath;
			link_dao.addDirectoryLink(clientid, entries[i].name, new_link_path);
		}
	}

	IMutex* dir_link_mutex;
}

std::string escape_glob_sql(const std::string& glob)
{
	std::string ret;
	ret.reserve(glob.size());
	for(size_t i=0;i<glob.size();++i)
	{
		if(glob[i]=='?')
		{
			ret+="[?]";
		}
		else if(glob[i]=='[')
		{
			ret+="[[]";
		}
		else if(glob[i]=='*')
		{
			ret+="[*]";
		}
		else
		{
			ret+=glob[i];
		}
	}
	return ret;
}


bool link_directory_pool( int clientid, const std::string& target_dir, const std::string& src_dir, const std::string& pooldir, bool with_transaction,
	ServerLinkDao*& link_dao, ServerLinkJournalDao*& link_journal_dao)
{
	if (link_dao == NULL)
	{
		link_dao=new ServerLinkDao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINKS));
	}

	DBScopedSynchronous synchonous_link_journal(NULL);

	if (!with_transaction && link_journal_dao == NULL)
	{
		IDatabase* link_journal_db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINK_JOURNAL);
		link_journal_dao = new ServerLinkJournalDao(link_journal_db);
		synchonous_link_journal.reset(link_journal_db);
	}

	DBScopedSynchronous synchonous_link(link_dao->getDatabase());

	IScopedLock lock(dir_link_mutex);

	DBScopedWriteTransaction link_transaction(link_dao->getDatabase());

	std::string link_src_dir;
	std::string pool_name;
	bool refcount_bigger_one=false;
	if(os_is_symlink(os_file_prefix(src_dir)))
	{
		if(!os_get_symlink_target(os_file_prefix(src_dir), link_src_dir))
		{
			Server->Log("Could not get symlink target of source directory \""+src_dir+"\".", LL_ERROR);
			return false;
		}

		pool_name = ExtractFileName(link_src_dir);

		if(pool_name.empty())
		{
			Server->Log("Error extracting pool name from link source \""+link_src_dir+"\"", LL_ERROR);
			return false;
		}

		link_dao->addDirectoryLink(clientid, pool_name, target_dir);
		reference_all_sublinks(*link_dao, clientid, src_dir, target_dir);
		refcount_bigger_one=true;
	}
	else if(os_directory_exists(os_file_prefix(src_dir)))
	{
		std::string parent_src_dir;
		do 
		{
			pool_name = ServerSettings::generateRandomAuthKey(10)+convert(Server->getTimeSeconds())+convert(Server->getTimeMS());
			parent_src_dir = pooldir + os_file_sep() + pool_name.substr(0, 2);
			link_src_dir = parent_src_dir + os_file_sep() + pool_name;
		} while (os_directory_exists(os_file_prefix(link_src_dir)));

		if(!os_directory_exists(os_file_prefix(parent_src_dir)) && !os_create_dir_recursive(os_file_prefix(parent_src_dir)))
		{
			Server->Log("Could not create directory for pool directory: \""+parent_src_dir+"\"", LL_ERROR);
			return false;
		}

		link_dao->addDirectoryLink(clientid, pool_name, src_dir);
		reference_all_sublinks(*link_dao, clientid, src_dir, target_dir);
		link_dao->addDirectoryLink(clientid, pool_name, target_dir);
				

		int64 replay_entry_id;
		if(!with_transaction)
		{
			link_journal_dao->addDirectoryLinkJournalEntry(src_dir, link_src_dir);
			replay_entry_id = link_dao->getLastId(); 
		}

		link_transaction.end();

		void* transaction=NULL;
		if(with_transaction)
		{
			transaction=os_start_transaction();

			if(!transaction)
			{
				Server->Log("Error starting filesystem transaction", LL_ERROR);
				link_dao->removeDirectoryLink(clientid, src_dir);
				link_dao->removeDirectoryLink(clientid, target_dir);
				return false;
			}
		}

		if(!os_rename_file(os_file_prefix(src_dir), os_file_prefix(link_src_dir), transaction))
		{
			Server->Log("Could not rename folder \""+src_dir+"\" to \""+link_src_dir+"\"", LL_ERROR);
			os_finish_transaction(transaction);
			link_dao->removeDirectoryLink(clientid, src_dir);
			link_dao->removeDirectoryLink(clientid, target_dir);
			return false;
		}

		if(!os_link_symbolic(os_file_prefix(link_src_dir), os_file_prefix(src_dir), transaction))
		{
			Server->Log("Could not create a symbolic link at \""+src_dir+"\" to \""+link_src_dir+"\"", LL_ERROR);
			os_rename_file(link_src_dir, src_dir, transaction);
			os_finish_transaction(transaction);
			link_dao->removeDirectoryLink(clientid, src_dir);
			link_dao->removeDirectoryLink(clientid, target_dir);
			return false;
		}

		if(with_transaction)
		{
			if(!os_finish_transaction(transaction))
			{
				Server->Log("Error finishing filesystem transaction", LL_ERROR);
				link_dao->removeDirectoryLink(clientid, src_dir);
				link_dao->removeDirectoryLink(clientid, target_dir);
				return false;
			}
		}
		else
		{
			link_journal_dao->removeDirectoryLinkJournalEntry(replay_entry_id);
		}
	}
	else
	{
		Server->Log("Cannot link directory \"" + target_dir + "\" because source directory \"" + src_dir + "\" does not exist.", LL_DEBUG);
		return false;
	}

	if(!os_link_symbolic(os_file_prefix(link_src_dir), os_file_prefix(target_dir)))
	{
		Server->Log("Error creating symbolic link from \"" + link_src_dir +"\" to \"" +
			target_dir+"\" -2", LL_ERROR);

		link_dao->removeDirectoryLink(clientid, target_dir);

		if(refcount_bigger_one)
		{
			link_dao->removeDirectoryLinkGlob(clientid, escape_glob_sql(target_dir)+os_file_sep()+"*");
		}

		return false;
	}

	return true;
}

bool replay_directory_link_journal( )
{
	IScopedLock lock(dir_link_mutex);

	ServerLinkJournalDao link_journal_dao(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER_LINK_JOURNAL));

	std::vector<ServerLinkJournalDao::JournalEntry> journal_entries = link_journal_dao.getDirectoryLinkJournalEntries();

	bool has_error=false;

	for(size_t i=0;i<journal_entries.size();++i)
	{
		const ServerLinkJournalDao::JournalEntry& je = journal_entries[i];

		std::string symlink_real_target;

		if(!os_is_symlink(je.linkname)
			|| (os_get_symlink_target(je.linkname, symlink_real_target) && symlink_real_target!=je.linktarget) )
		{
			if(os_directory_exists(je.linktarget))
			{
				os_remove_symlink_dir(os_file_prefix(je.linkname));
				if(!os_link_symbolic(os_file_prefix(je.linktarget), os_file_prefix(je.linkname)))
				{
					Server->Log("Error replaying symlink journal: Could create link at \""+je.linkname+"\" to \""+je.linktarget+"\"", LL_ERROR);
					has_error=true;
				}
			}
		}
	}

	link_journal_dao.removeDirectoryLinkJournalEntries();

	return has_error;
}

bool remove_directory_link(const std::string & path, ServerLinkDao & link_dao, int clientid,
	std::auto_ptr<DBScopedSynchronous>& synchronous_link_dao, bool with_transaction)
{
	std::string pool_path;
	if (!os_get_symlink_target(path, pool_path))
	{
		Server->Log("Error getting symlink path in pool of \"" + path + "\"", LL_ERROR);
		return false;
	}

	std::string pool_name = ExtractFileName(pool_path, os_file_sep());

	if (pool_name.empty())
	{
		Server->Log("Error extracting pool name from pool path \"" + pool_path + "\"", LL_ERROR);
		return false;
	}

	std::string directory_pool = ExtractFileName(ExtractFilePath(ExtractFilePath(pool_path, os_file_sep()), os_file_sep()), os_file_sep());

	if (directory_pool != ".directory_pool")
	{
		//Other symlink. Simply delete
		if (!os_remove_symlink_dir(os_file_prefix(path)))
		{
			Server->Log("Error removing symlink dir \"" + path + "\"", LL_ERROR);
		}
		return true;
	}

	std::string target_raw;
	if (next(path, 0, os_file_prefix("")))
	{
		target_raw = path.substr(os_file_prefix("").size());
	}
	else
	{
		target_raw = path;
	}

	if (with_transaction)
	{
		synchronous_link_dao.reset(new DBScopedSynchronous(link_dao.getDatabase()));
		link_dao.getDatabase()->BeginWriteTransaction();
	}

	link_dao.removeDirectoryLink(clientid, target_raw);

	if (link_dao.getLastChanges()>0)
	{
		bool ret = true;
		if (link_dao.getDirectoryRefcount(clientid, pool_name) == 0)
		{
			ret = remove_directory_link_dir(path, link_dao, clientid, false, false);
			ret = ret && os_remove_dir(os_file_prefix(pool_path));

			if (!ret)
			{
				Server->Log("Error removing directory link \"" + path + "\" with pool path \"" + pool_path + "\"", LL_ERROR);
			}
		}
		else
		{
			link_dao.removeDirectoryLinkGlob(clientid, escape_glob_sql(target_raw) + os_file_sep() + "*");
		}
	}
	else
	{
		Server->Log("Directory link \"" + path + "\" with pool path \"" + pool_path + "\" not found in database. Deleting symlink only.", LL_WARNING);
	}


	if (!os_remove_symlink_dir(os_file_prefix(path)))
	{
		Server->Log("Error removing symlink dir \"" + path + "\"", LL_ERROR);
	}

	{
		std::auto_ptr<IFile> dir_f(Server->openFile(os_file_prefix(ExtractFilePath(path, os_file_sep())), MODE_READ_SEQUENTIAL_BACKUP));
		if (dir_f.get() != NULL)
		{
			dir_f->Sync();
		}
	}

	if (with_transaction)
	{
		link_dao.getDatabase()->EndTransaction();;
		synchronous_link_dao.reset();
	}

	return true;
}

namespace
{
	struct SSymlinkCallbackData
	{
		SSymlinkCallbackData(ServerLinkDao* link_dao,
			int clientid, bool with_transaction)
			: link_dao(link_dao), clientid(clientid),
			with_transaction(with_transaction)
		{

		}

		ServerLinkDao* link_dao;
		std::auto_ptr<DBScopedSynchronous> synchronous_link_dao;
		int clientid;
		bool with_transaction;
	};

	bool symlink_callback(const std::string &path, void* userdata)
	{
		SSymlinkCallbackData* data = reinterpret_cast<SSymlinkCallbackData*>(userdata);

		return remove_directory_link(path, *data->link_dao, data->clientid,
			data->synchronous_link_dao, data->with_transaction);
	}
}

bool remove_directory_link_dir(const std::string &path, ServerLinkDao& link_dao, int clientid, bool delete_root, bool with_transaction)
{
	IScopedLock lock(dir_link_mutex);

	SSymlinkCallbackData userdata(&link_dao, clientid, with_transaction);
	return os_remove_nonempty_dir(os_file_prefix(path), symlink_callback, &userdata, delete_root);
}

bool is_directory_link(const std::string & path)
{
	int ftype = os_get_file_type(os_file_prefix(path));

	if (ftype & EFileType_Directory == 0
		|| ftype & EFileType_Symlink == 0)
	{
		return false;
	}

	std::string symlink_target;
	if (!os_get_symlink_target(os_file_prefix(path), symlink_target))
	{
		return false;
	}

	std::string directory_pool = ExtractFileName(ExtractFilePath(ExtractFilePath(symlink_target, os_file_sep()), os_file_sep()), os_file_sep());

	return directory_pool == ".directory_pool";
}

void init_dir_link_mutex()
{
	dir_link_mutex=Server->createMutex();
}

void destroy_dir_link_mutex()
{
	Server->destroy(dir_link_mutex);
}
