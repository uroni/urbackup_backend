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

#ifndef CLIENT_ONLY

#include "action_header.h"
#include "login.h"

#ifndef _WIN32
#include <syslog.h>
#endif

namespace
{
#ifndef _WIN32
	class InitSyslog
	{
	public:
		InitSyslog()
		{
			openlog(NULL, 0, LOG_USER);
		}
	};

	InitSyslog initSyslog;
#endif
}

std::string loginMethodToString(LoginMethod lm)
{
	switch(lm)
	{
	case LoginMethod_RestoreCD: return "restore CD";
	case LoginMethod_Webinterface: return "web interface";
	}
	return std::string();
}

void logSuccessfullLogin(Helper& helper, str_nmap& PARAMS, const std::wstring& username, LoginMethod method)
{
	IQuery* q = helper.getDatabase()->Prepare("INSERT INTO settings_db.login_access_log (username, ip, method)"
		" VALUES (?, ?, ?)");

	q->Bind(username);
	q->Bind(PARAMS["REMOTE_ADDR"]);
	q->Bind(static_cast<int>(method));
	q->Write();
	q->Reset();

#ifndef _WIN32
	syslog(LOG_AUTH|LOG_INFO, "Login successful for %S from %s via %s", username.c_str(), PARAMS["REMOTE_ADDR"].c_str(), loginMethodToString(method).c_str());
#endif
}

void logFailedLogin(Helper& helper, str_nmap& PARAMS, const std::wstring& username, LoginMethod method)
{
#ifndef _WIN32
	syslog(LOG_AUTH|LOG_INFO, "Authentication failure for %S from %s via %s", username.c_str(), PARAMS["REMOTE_ADDR"].c_str(), loginMethodToString(method).c_str());
#endif
}

ACTION_IMPL(login)
{
	JSON::Object ret;

	ret.set("api_version", 1);

	{
		IScopedLock lock(startup_status.mutex);
		if(startup_status.creating_filesindex)
		{
			Helper helper(tid, &GET, &PARAMS);
			ret.set("lang", helper.getLanguage());
			ret.set("creating_filescache", startup_status.creating_filesindex);
			ret.set("processed_file_entries", startup_status.processed_file_entries);
			ret.set("percent_finished", startup_status.pc_done*100.0);
            Server->Write( tid, ret.stringify(false) );
			return;
		}
		else if(startup_status.upgrading_database)
		{
			Helper helper(tid, &GET, &PARAMS);
			ret.set("lang", helper.getLanguage());
			ret.set("upgrading_database", startup_status.upgrading_database);
			ret.set("curr_db_version", startup_status.curr_db_version);
			ret.set("target_db_version", startup_status.target_db_version);
            Server->Write( tid, ret.stringify(false) );
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
		bool plainpw=GET[L"plainpw"]==L"1";
		if(!has_session && plainpw)
		{
			ses=helper.generateSession(L"anonymous");
			GET[L"ses"]=ses;
			ret.set("session", JSON::Value(ses));
			helper.update(tid, &GET, &PARAMS);
		}
		SUser *session=helper.getSession();
		if(session!=NULL)
		{
			int user_id = SESSION_ID_TOKEN_AUTH;
			
			if(helper.checkPassword(username, GET[L"password"], &user_id, plainpw) ||
				(plainpw && helper.ldapLogin(username, GET[L"password"])) )
			{
				ret.set("success", JSON::Value(true));
				logSuccessfullLogin(helper, PARAMS, username, LoginMethod_Webinterface);
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
				logFailedLogin(helper, PARAMS, username, LoginMethod_Webinterface);
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
		bool ldap_enabled = helper.ldapEnabled();
		db_results res=db->Read("SELECT count(*) AS c FROM settings_db.si_users");
		if( (!res.empty() && watoi(res[0][L"c"])>0) || ldap_enabled)
		{
			ret.set("success", JSON::Value(false) );
			if(ldap_enabled)
			{
				ret.set("ldap_enabled", true);
			}
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
				logSuccessfullLogin(helper, PARAMS, L"anonymous", LoginMethod_Webinterface);
				session->mStr[L"login"]=L"ok";
				session->id=SESSION_ID_ADMIN;
			}
			else
			{
				ret.set("error", JSON::Value(1));
			}
		}
	}

    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
