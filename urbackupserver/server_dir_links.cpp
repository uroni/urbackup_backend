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

#include "server_dir_links.h"
#include "../urbackupcommon/os_functions.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include "server_settings.h"
#include "../Interface/Mutex.h"

namespace
{
	void reference_all_sublinks(ServerBackupDao& backup_dao, int clientid, const std::wstring& target, const std::wstring& new_target)
	{
		std::wstring escaped_target = escape_glob_sql(target);

		std::vector<ServerBackupDao::DirectoryLinkEntry> entries = backup_dao.getLinksInDirectory(clientid, escaped_target+os_file_sep()+L"*");

		for(size_t i=0;i<entries.size();++i)
		{
			std::wstring subpath = entries[i].target.substr(target.size());
			std::wstring new_link_path = new_target + subpath;
			backup_dao.addDirectoryLink(clientid, entries[i].name, new_link_path);
		}
	}

	IMutex* dir_link_mutex;
}

std::wstring escape_glob_sql(const std::wstring& glob)
{
	std::wstring ret;
	ret.reserve(glob.size());
	for(size_t i=0;i<glob.size();++i)
	{
		if(glob[i]=='?')
		{
			ret+=L"[?]";
		}
		else if(glob[i]=='[')
		{
			ret+=L"[[]";
		}
		else if(glob[i]=='*')
		{
			ret+=L"[*]";
		}
		else
		{
			ret+=glob[i];
		}
	}
	return ret;
}


bool link_directory_pool( ServerBackupDao& backup_dao, int clientid, const std::wstring& target_dir, const std::wstring& src_dir, const std::wstring& pooldir, bool with_transaction )
{
	IScopedLock lock(dir_link_mutex);

	std::wstring link_src_dir;
	std::wstring pool_name;
	bool refcount_bigger_one=false;
	if(os_is_symlink(os_file_prefix(src_dir)))
	{
		if(!os_get_symlink_target(os_file_prefix(src_dir), link_src_dir))
		{
			Server->Log(L"Could not get symlink target of source directory \""+src_dir+L"\".", LL_ERROR);
			return false;
		}

		pool_name = ExtractFileName(link_src_dir);

		if(pool_name.empty())
		{
			Server->Log(L"Error extracting pool name from link source \""+link_src_dir+L"\"", LL_ERROR);
			return false;
		}

		backup_dao.addDirectoryLink(clientid, pool_name, target_dir);
		reference_all_sublinks(backup_dao, clientid, src_dir, target_dir);
		backup_dao.commit();
		refcount_bigger_one=true;
	}
	else if(os_directory_exists(os_file_prefix(src_dir)))
	{
		std::wstring parent_src_dir;
		do 
		{
			pool_name = widen(ServerSettings::generateRandomAuthKey(10))+convert(Server->getTimeSeconds())+convert(Server->getTimeMS());
			parent_src_dir = pooldir + os_file_sep() + pool_name.substr(0, 2);
			link_src_dir = parent_src_dir + os_file_sep() + pool_name;
		} while (os_directory_exists(os_file_prefix(link_src_dir)));

		if(!os_directory_exists(os_file_prefix(parent_src_dir)) && !os_create_dir_recursive(os_file_prefix(parent_src_dir)))
		{
			Server->Log(L"Could not create directory for pool directory: \""+parent_src_dir+L"\"", LL_ERROR);
			return false;
		}

		backup_dao.addDirectoryLink(clientid, pool_name, src_dir);
		reference_all_sublinks(backup_dao, clientid, src_dir, target_dir);
		backup_dao.addDirectoryLink(clientid, pool_name, target_dir);
				

		int64 replay_entry_id;
		if(!with_transaction)
		{
			backup_dao.addDirectoryLinkJournalEntry(src_dir, link_src_dir);
			replay_entry_id = backup_dao.getLastId(); 
		}

		backup_dao.commit();	

		void* transaction=NULL;
		if(with_transaction)
		{
			transaction=os_start_transaction();

			if(!transaction)
			{
				Server->Log("Error starting filesystem transaction", LL_ERROR);
				backup_dao.removeDirectoryLink(clientid, src_dir);
				backup_dao.removeDirectoryLink(clientid, target_dir);
				return false;
			}
		}

		if(!os_rename_file(os_file_prefix(src_dir), os_file_prefix(link_src_dir), transaction))
		{
			Server->Log(L"Could not rename folder \""+src_dir+L"\" to \""+link_src_dir+L"\"", LL_ERROR);
			os_finish_transaction(transaction);
			backup_dao.removeDirectoryLink(clientid, src_dir);
			backup_dao.removeDirectoryLink(clientid, target_dir);
			return false;
		}

		if(!os_link_symbolic(os_file_prefix(link_src_dir), os_file_prefix(src_dir), transaction))
		{
			Server->Log(L"Could not create a symbolic link at \""+src_dir+L"\" to \""+link_src_dir+L"\"", LL_ERROR);
			os_rename_file(link_src_dir, src_dir, transaction);
			os_finish_transaction(transaction);
			backup_dao.removeDirectoryLink(clientid, src_dir);
			backup_dao.removeDirectoryLink(clientid, target_dir);
			return false;
		}

		if(with_transaction)
		{
			if(!os_finish_transaction(transaction))
			{
				Server->Log("Error finishing filesystem transaction", LL_ERROR);
				backup_dao.removeDirectoryLink(clientid, src_dir);
				backup_dao.removeDirectoryLink(clientid, target_dir);
				return false;
			}
		}
		else
		{
			backup_dao.removeDirectoryLinkJournalEntry(replay_entry_id);
		}
	}
	else
	{
		return false;
	}

	if(!os_link_symbolic(os_file_prefix(link_src_dir), os_file_prefix(target_dir)))
	{
		Server->Log(L"Error creating symbolic link from \"" + link_src_dir +L"\" to \"" +
			target_dir+L"\" -2", LL_ERROR);

		backup_dao.removeDirectoryLink(clientid, target_dir);

		if(refcount_bigger_one)
		{
			backup_dao.removeDirectoryLinkGlob(clientid, escape_glob_sql(target_dir)+os_file_sep()+L"*");
		}

		return false;
	}

	return true;
}

bool replay_directory_link_journal( ServerBackupDao& backup_dao )
{
	IScopedLock lock(dir_link_mutex);

	std::vector<ServerBackupDao::JournalEntry> journal_entries = backup_dao.getDirectoryLinkJournalEntries();

	bool has_error=false;

	for(size_t i=0;i<journal_entries.size();++i)
	{
		const ServerBackupDao::JournalEntry& je = journal_entries[i];

		std::wstring symlink_real_target;

		if(!os_is_symlink(je.linkname)
			|| (os_get_symlink_target(je.linkname, symlink_real_target) && symlink_real_target!=je.linktarget) )
		{
			if(os_directory_exists(je.linktarget))
			{
				os_remove_symlink_dir(os_file_prefix(je.linkname));
				if(!os_link_symbolic(os_file_prefix(je.linktarget), os_file_prefix(je.linkname)))
				{
					Server->Log(L"Error replaying symlink journal: Could create link at \""+je.linkname+L"\" to \""+je.linktarget+L"\"", LL_ERROR);
					has_error=true;
				}
			}
		}
	}

	backup_dao.removeDirectoryLinkJournalEntries();

	return has_error;
}

namespace
{
	struct SSymlinkCallbackData
	{
		SSymlinkCallbackData(ServerBackupDao* backup_dao,
			int clientid, bool with_transaction)
			: backup_dao(backup_dao), clientid(clientid),
			with_transaction(with_transaction)
		{

		}

		ServerBackupDao* backup_dao;
		int clientid;
		bool with_transaction;
	};

	bool symlink_callback(const std::wstring &path, void* userdata)
	{
		SSymlinkCallbackData* data = reinterpret_cast<SSymlinkCallbackData*>(userdata);

		std::wstring pool_path;
		if(!os_get_symlink_target(path, pool_path))
		{
			Server->Log(L"Error getting symlink path in pool of \""+path+L"\"", LL_ERROR);
			return false;
		}

		std::wstring pool_name = ExtractFileName(pool_path, os_file_sep());

		if(pool_name.empty())
		{
			Server->Log(L"Error extracting pool name from pool path \""+pool_path+L"\"", LL_ERROR);
			return false;
		}

		std::wstring directory_pool = ExtractFileName(ExtractFilePath(ExtractFilePath(pool_path, os_file_sep()), os_file_sep()), os_file_sep());

		if(directory_pool!=L".directory_pool")
		{
			//Other symlink. Simply delete
			if(!os_remove_symlink_dir(os_file_prefix(path)))
			{
				Server->Log(L"Error removing symlink dir \""+path+L"\"", LL_ERROR);
			}
			return true;
		}

		std::wstring target_raw;
		if(next(path,0, os_file_prefix(L"")))
		{
			target_raw = path.substr(os_file_prefix(L"").size());
		}
		else
		{
			target_raw = path;
		}

		if(data->with_transaction)
		{
			data->backup_dao->BeginWriteTransaction();
		}		

		data->backup_dao->removeDirectoryLink(data->clientid, target_raw);

		if(data->backup_dao->getLastChanges()>0)
		{
			bool ret = true;
			if(data->backup_dao->getDirectoryRefcount(data->clientid, pool_name)==0)
			{
				ret = remove_directory_link_dir(path, *data->backup_dao, data->clientid, false, false);
				ret = ret && os_remove_dir(os_file_prefix(pool_path));

				if(!ret)
				{
					Server->Log(L"Error removing directory link \""+path+L"\" with pool path \""+pool_path+L"\"", LL_ERROR);
				}
			}
			else
			{
				data->backup_dao->removeDirectoryLinkGlob(data->clientid, escape_glob_sql(target_raw)+os_file_sep()+L"*");
			}
		}
		else
		{
			Server->Log(L"Directory link \""+path+L"\" with pool path \""+pool_path+L"\" not found in database. Deleting symlink only.", LL_WARNING);
		}
		

		if(!os_remove_symlink_dir(os_file_prefix(path)))
		{
			Server->Log(L"Error removing symlink dir \""+path+L"\"", LL_ERROR);
		}

		if(data->with_transaction)
		{
			data->backup_dao->endTransaction();
		}

		return true;
	}
}

bool remove_directory_link_dir(const std::wstring &path, ServerBackupDao& backup_dao, int clientid, bool delete_root, bool with_transaction)
{
	IScopedLock lock(dir_link_mutex);

	SSymlinkCallbackData userdata(&backup_dao, clientid, with_transaction);
	return os_remove_nonempty_dir(os_file_prefix(path), symlink_callback, &userdata, delete_root);
}

void init_dir_link_mutex()
{
	dir_link_mutex=Server->createMutex();
}

void destroy_dir_link_mutex()
{
	Server->destroy(dir_link_mutex);
}
