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

ACTION_IMPL(piegraph)
{
	Helper helper(tid, &GET, &PARAMS);

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL && helper.getRights("piegraph")=="all" )
	{
		IDatabase *db=helper.getDatabase();
		IQuery *q=db->Prepare("SELECT (bytes_used_files+bytes_used_images) AS used, name FROM clients ORDER BY (bytes_used_files+bytes_used_images) DESC");
		db_results res=q->Read();

		SPieInfo pi;
		for(size_t i=0;i<res.size();++i)
		{
			pi.data.push_back((float)atof(wnarrow(res[i][L"used"]).c_str()));
			pi.labels.push_back(Server->ConvertToUTF8(res[i][L"name"]));
		}

		pi.title="";
		pi.sizex=700;
		pi.sizey=700;

		if(pychart_fak!=NULL)
		{
			IPychart *pychart=pychart_fak->getPychart();
			unsigned int id=pychart->drawPie(pi);
			session->mInt[L"image_"+convert(id)]=1;
			ret.set("image_id", id);
		}
		else
		{
			ret.set("error", JSON::Value(3));
		}
	}	
	else
	{
		ret.set("error", JSON::Value(1));
	}
	
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY