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

ACTION_IMPL(isimageready)
{
	Helper helper(tid, &GET, &PARAMS);

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL )
	{
		int img_id=watoi(GET[L"image_id"]);
		int_map::iterator iter=session->mInt.find(L"image_"+convert(img_id));
		if(iter!=session->mInt.end())
		{
			IPychart *pychart=pychart_fak->getPychart();
			IFile *img=pychart->queryForFile(img_id);
			if(img!=NULL)
			{
				session->mInt.erase(iter);
				session->mCustom["image_"+nconvert(img_id)]=img;
				ret.set("image_ready", true);
			}
			else
			{
				ret.set("image_ready", false);
			}
		}
		else
		{
			ret.set("error", 2);
		}
	}	
	else
	{
		ret.set("error", JSON::Value(1));
	}
	
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY