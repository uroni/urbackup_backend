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

ACTION_IMPL(salt)
{
	Helper helper(tid, &GET, &PARAMS);

	JSON::Object ret;
	std::wstring username=GET[L"username"];
	if(helper.getSession()==NULL && !username.empty())
	{
		std::wstring ses=helper.generateSession(username);
		ret.set("ses", JSON::Value(ses));
		GET[L"ses"]=ses;
		helper.update(tid, &GET, &PARAMS);
	}

	SUser *session=helper.getSession();
	if(session!=NULL)
	{
		if(username.empty())
		{
			str_map::iterator iter=session->mStr.find(L"username");
			if(iter!=session->mStr.end())
			{
				username=iter->second;
			}
		}
		IQuery *q=helper.getDatabase()->Prepare("SELECT salt FROM settings_db.si_users WHERE name=?");
		q->Bind(username);
		db_results res=q->Read();
	
		if(res.empty())
		{
			ret.set("error", JSON::Value(0));
		}
		else
		{
			ret.set("salt", res[0][L"salt"]);
			std::string rnd=ServerSettings::generateRandomAuthKey();
			ret.set("rnd", JSON::Value(rnd));
			session->mStr[L"rnd"]=widen(rnd);
		}
	}
	else
	{
		ret.set("error", JSON::Value(1));
	}
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY