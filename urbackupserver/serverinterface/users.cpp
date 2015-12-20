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

ACTION_IMPL(users)
{
	Helper helper(tid, &POST, &PARAMS);

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL && helper.getRights("users")=="all" )
	{
		IDatabase *db=helper.getDatabase();
		IQuery *q=db->Prepare("SELECT id, name FROM clients ORDER BY name");
		db_results res=q->Read();
		
		JSON::Array users;
		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object obj;
			obj.set("id",watoi(res[i]["id"]));
			obj.set("name",res[i]["name"]);
			users.add(obj);
		}
		ret.set("users", users);
	}
	else
	{
		ret.set("error", 1);
	}
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
