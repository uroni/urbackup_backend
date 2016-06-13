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

#include "tokens.h"
#include "../Interface/Server.h"
#include "database.h"
#include "../stringtools.h"
#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#endif

namespace tokens
{

bool write_tokens()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	ClientDAO dao(db);

	std::string hostname = get_hostname();

	db->BeginWriteTransaction();

	bool has_new_token=false;
	std::vector<std::string> users = get_users();

	os_create_dir(tokens_path);

#ifndef _WIN32
	chmod(tokens_path, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
#endif

	std::vector<SFile> files = getFilesWin(tokens_path, NULL, false, false);

	for(size_t i=0;i<users.size();++i)
	{
		std::string user_fn="user_"+bytesToHex((users[i]));
		std::string token_fn = tokens_path + os_file_sep()+user_fn;
		bool file_found=false;
		for(size_t j=0;j<files.size();++j)
		{
			if(files[j].name==user_fn)
			{
				file_found=true;
				break;
			}
		}

		if(file_found)
		{
			continue;
		}

		has_new_token |= write_token(hostname, true, users[i], token_fn, dao);
	}

	std::vector<std::string> groups = get_groups();

	for(size_t i=0;i<groups.size();++i)
	{
		std::string group_fn="group_"+bytesToHex((groups[i]));
		std::string token_fn = tokens_path + os_file_sep()+group_fn;
		bool file_found=false;
		for(size_t j=0;j<files.size();++j)
		{
			if(files[j].name==group_fn)
			{
				file_found=true;
				break;
			}
		}

		if(file_found)
		{
			continue;
		}

		has_new_token |= write_token(hostname, false, groups[i], token_fn, dao);

	}

	if(has_new_token)
	{
		//dao.updateMiscValue("has_new_token", L"true");

		if(has_new_token)
		{
			for(size_t i=0;i<users.size();++i)
			{
				std::vector<std::string> user_groups = get_user_groups(users[i]);

				ClientDAO::CondInt64 uid = dao.getFileAccessTokenId(users[i], 1);
				if(uid.exists)
				{
					for(size_t j=0;j<user_groups.size();++j)
					{
						dao.updateGroupMembership(uid.value, user_groups[j]);
					}
				}

			}
		}
	}

	db->EndTransaction();

	return has_new_token;
}

} //namespace tokens
