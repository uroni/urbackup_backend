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

#ifdef _WIN32
const wchar_t* tokens_path = L"tokens";
#else
const wchar_t* tokens_path = L"urbackup/tokens";
#endif

bool write_tokens()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	ClientDAO dao(db);

	std::wstring hostname = get_hostname();

	db->BeginTransaction();

	bool has_new_token=false;
	std::vector<std::wstring> users = get_users();

	os_create_dir(tokens_path);

#ifndef _WIN32
	chmod(Server->ConvertToUTF8(tokens_path).c_str(), S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
#endif

	std::vector<SFile> files = getFilesWin(tokens_path, NULL, false, false);

	for(size_t i=0;i<users.size();++i)
	{
		std::wstring user_fn=L"user_"+widen(bytesToHex(Server->ConvertToUTF8(users[i])));
		std::wstring token_fn = tokens_path + os_file_sep()+user_fn;
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

	std::vector<std::wstring> groups = get_groups();

	for(size_t i=0;i<groups.size();++i)
	{
		std::wstring group_fn=L"group_"+widen(bytesToHex(Server->ConvertToUTF8(groups[i])));
		std::wstring token_fn = tokens_path + os_file_sep()+group_fn;
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
		dao.updateMiscValue("has_new_token", L"true");

		if(has_new_token)
		{
			for(size_t i=0;i<users.size();++i)
			{
				std::vector<std::wstring> user_groups = get_user_groups(users[i]);

				ClientDAO::CondInt uid = dao.getFileAccessTokenId(users[i], 1);
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
