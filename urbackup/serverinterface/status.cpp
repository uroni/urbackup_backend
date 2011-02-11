/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#ifndef CLIENT_ONLY

#include "action_header.h"
#include "../server_settings.h"
#include "../os_functions.h"

ACTION_IMPL(status)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;

	std::string rights=helper.getRights("status");
	std::vector<int> clientids;
	IDatabase *db=helper.getDatabase();
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientids.push_back(atoi(s_clientid[i].c_str()));
		}
	}

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL && (rights=="all" || !clientids.empty()) )
	{
		{
			ServerSettings settings(db);
			if(!os_directory_exists(settings.getSettings()->backupfolder) || !os_directory_exists(settings.getSettings()->backupfolder_uncompr) || settings.getSettings()->backupfolder.empty())
			{
				ret.set("dir_error", true);
			}
		}


		JSON::Array status;
		IDatabase *db=helper.getDatabase();
		std::string filter;
		if(!clientids.empty())
		{
			filter=" WHERE ";
			for(size_t i=0;i<clientids.size();++i)
			{
				filter+="id="+nconvert(clientids[i]);
				if(i+1<clientids.size())
					filter+=" OR ";
			}
		}
		db_results res=db->Read("SELECT id, name, strftime('"+helper.getTimeFormatString()+"', lastbackup, 'localtime') AS lastbackup, strftime('"+time_format_str+"', lastseen, 'localtime') AS lastseen,"
			"strftime('"+helper.getTimeFormatString()+"', lastbackup_image, 'localtime') AS lastbackup_image FROM clients"+filter);

		int backup_ok_mod=3;
		db_results res_t=db->Read("SELECT value FROM settings WHERE key='backup_ok_mod' AND clientid=0");
		if(res_t.size()>0)
		{
			backup_ok_mod=watoi(res_t[0][L"value"]);
		}

		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object stat;
			int clientid=watoi(res[i][L"id"]);
			stat.set("id", clientid);
			stat.set("name", res[i][L"name"]);
			stat.set("lastbackup", res[i][L"lastbackup"]);
			stat.set("lastseen", res[i][L"lastseen"]);
			stat.set("lastbackup_image", res[i][L"lastbackup_image"]);

			ServerSettings settings(db, clientid);
			IQuery *q=db->Prepare("SELECT id FROM clients WHERE lastbackup IS NOT NULL AND datetime('now','-"+nconvert(settings.getSettings()->update_freq_incr*backup_ok_mod)+" seconds')<lastbackup AND id=?");
			q->Bind(clientid);
			db_results res_file_ok=q->Read();
			q->Reset();
			stat.set("file_ok", !res_file_ok.empty());

			q=db->Prepare("SELECT id FROM clients WHERE lastbackup_image IS NOT NULL AND datetime('now','-"+nconvert(settings.getSettings()->update_freq_image_incr*backup_ok_mod)+" seconds')<lastbackup_image AND id=?");
			q->Bind(clientid);
			res_file_ok=q->Read();
			q->Reset();
			stat.set("image_ok", !res_file_ok.empty());

			status.add(stat);
		}
		ret.set("status", status);

	}
	else
	{
		ret.set("error", 1);
	}
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY