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

	DBScopedWriteTransaction write_transaction(db);

	bool has_new_token=false;
	std::vector<std::string> users = get_local_users();

	os_create_dir(tokens_path);

#ifndef _WIN32
	chmod(tokens_path, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
#endif

	std::vector<SFile> files = getFilesWin(tokens_path, NULL, false, false);

	for(size_t i=0;i<users.size();++i)
	{
		std::string user_fn="user_"+bytesToHex(accountname_normalize(users[i]));
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

		std::vector<std::string> user_groups = get_user_groups(users[i]);

		for (size_t j=0;j<user_groups.size();++j)
		{
			std::string group_fn = "group_" + bytesToHex(accountname_normalize(user_groups[j]));
			std::string token_fn = tokens_path + os_file_sep() + group_fn;
			bool file_found = false;
			for (size_t j = 0; j<files.size(); ++j)
			{
				if (files[j].name == group_fn)
				{
					file_found = true;
					break;
				}
			}

			if (file_found)
			{
				continue;
			}

			has_new_token |= write_token(hostname, false, user_groups[j], token_fn, dao);
		}

		if (has_new_token)
		{
			ClientDAO::CondInt64 uid = dao.getFileAccessTokenId2Alts(accountname_normalize(users[i]), ClientDAO::c_is_system_user, ClientDAO::c_is_user);
			if (uid.exists)
			{
				dao.removeGroupMembership(uid.value);
				for (size_t j = 0; j < user_groups.size(); ++j)
				{
					dao.updateGroupMembership(uid.value, accountname_normalize(user_groups[j]));
				}
			}
		}
	}

	return has_new_token;
}

} //namespace tokens
