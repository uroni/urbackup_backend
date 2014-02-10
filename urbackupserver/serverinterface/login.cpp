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

ACTION_IMPL(login)
{
	JSON::Object ret;

	{
		IScopedLock lock(startup_status.mutex);
		if(startup_status.creating_filescache)
		{
			Helper helper(tid, &GET, &PARAMS);
			ret.set("lang", helper.getLanguage());
			ret.set("creating_filescache", startup_status.creating_filescache);
			ret.set("processed_file_entries", startup_status.processed_file_entries);
			Server->Write( tid, ret.get(false) );
			return;
		}
		else if(startup_status.upgrading_database)
		{
			Helper helper(tid, &GET, &PARAMS);
			ret.set("lang", helper.getLanguage());
			ret.set("upgrading_database", startup_status.upgrading_database);
			ret.set("curr_db_version", startup_status.curr_db_version);
			ret.set("target_db_version", startup_status.target_db_version);
			Server->Write( tid, ret.get(false) );
			return;
		}
	}

	Helper helper(tid, &GET, &PARAMS);
	IDatabase *db=helper.getDatabase();

	bool has_session=false;
	std::wstring ses;
	if(!GET[L"ses"].empty())
	{
		ses=GET[L"ses"];
		has_session=true;
	}

	std::wstring username=GET[L"username"];
	if(!username.empty())
	{
		/*if(has_session==false)
		{
			ses=helper.generateSession(L"anonymous");
			GET[L"ses"]=ses;
			ret.set("session", JSON::Value(ses));
			helper.update(tid, &GET, &PARAMS);
		}*/
		SUser *session=helper.getSession();
		if(session!=NULL)
		{
			int user_id;
			if(helper.checkPassword(username, GET[L"password"], &user_id) )
			{
				ret.set("success", JSON::Value(true));
				session->mStr[L"login"]=L"ok";
				session->mStr[L"username"]=username;
				session->id=user_id;

				ret.set("status", helper.getRights("status") );
				ret.set("graph", helper.getRights("piegraph"));
				ret.set("progress", helper.getRights("progress") );
				ret.set("browse_backups", helper.getRights("browse_backups") );
				ret.set("settings", helper.getRights("settings") );
				ret.set("logs", helper.getRights("logs") );
			}
			else
			{
				Server->wait(1000);
				ret.set("error", JSON::Value(2));
			}
		}
		else
		{
			ret.set("error", JSON::Value(1));
		}
	}
	else
	{
		ret.set("lang", helper.getLanguage());
		db_results res=db->Read("SELECT count(*) AS c FROM settings_db.si_users");
		if(!res.empty() && watoi(res[0][L"c"])>0)
		{
			ret.set("success", JSON::Value(false) );
		}
		else
		{
			ret.set("success", JSON::Value(true) );
			if(has_session==false)
			{
				ses=helper.generateSession(L"anonymous");
				GET[L"ses"]=ses;
				ret.set("session", JSON::Value(ses));
				helper.update(tid, &GET, &PARAMS);
			}
			SUser *session=helper.getSession();
			if(session!=NULL)
			{
				session->mStr[L"login"]=L"ok";
				session->id=0;
			}
			else
			{
				ret.set("error", JSON::Value(1));
			}
		}
	}

	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY