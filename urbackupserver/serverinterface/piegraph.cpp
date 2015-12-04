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

ACTION_IMPL(piegraph)
{
	Helper helper(tid, &GET, &PARAMS);

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL && helper.getRights("piegraph")=="all" )
	{
		helper.releaseAll();

		IDatabase *db=helper.getDatabase();
		IQuery *q=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used, name FROM clients ORDER BY (bytes_used_files+bytes_used_images) DESC");
		db_results res=q->Read();

		JSON::Array data;
		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object obj;
			obj.set("data", ((float)atof(wnarrow(res[i][L"used"]).c_str())));
			obj.set("label", Server->ConvertToUTF8(res[i][L"name"]));
			data.add(obj);
		}
		ret.set("data", data);		
	}	
	else
	{
		ret.set("error", JSON::Value(1));
	}
	
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
