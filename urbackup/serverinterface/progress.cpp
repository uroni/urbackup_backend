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
#include "../server_status.h"

void getLastActs(Helper &helper, JSON::Object &ret, std::vector<int> clientids);

ACTION_IMPL(progress)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;

	std::vector<int> clientids;
	std::string rights=helper.getRights("progress");
	if(rights!="none" && rights!="all")
	{
		std::vector<std::string> s_cid;
		Tokenize(rights, s_cid, ",");
		for(size_t i=0;i<s_cid.size();++i)
		{
			clientids.push_back(atoi(s_cid[i].c_str()));
		}
	}

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL && (rights=="all" || !clientids.empty()) )
	{
		JSON::Array pg;
		std::vector<SStatus> clients=ServerStatus::getStatus();
		for(size_t i=0;i<clients.size();++i)
		{
			bool found=false;
			for(size_t j=0;j<clientids.size();++j)
			{
				if(clientids[j]==clients[i].clientid)
				{
					found=true;
					break;
				}
			}
			if(clients[i].r_online && clients[i].statusaction!=sa_none && (rights=="all" || found==true) )
			{
				JSON::Object obj;
				obj.set("name", JSON::Value(clients[i].client));
				obj.set("action", JSON::Value((int)clients[i].statusaction));
				obj.set("pcdone", JSON::Value(clients[i].pcdone));
				obj.set("queue", JSON::Value(clients[i].prepare_hashqueuesize+clients[i].hashqueuesize) );
				pg.add(obj);
			}
		}
		ret.set("progress", pg);
	}
	else
	{
		ret.set("error", JSON::Value(1));
	}

	clientids.clear();
	rights=helper.getRights("lastacts");
	if(rights!="none" && rights!="all")
	{
		std::vector<std::string> s_cid;
		Tokenize(rights, s_cid, ",");
		for(size_t i=0;i<s_cid.size();++i)
		{
			clientids.push_back(atoi(s_cid[i].c_str()));
		}
	}

	if(session!=NULL && (rights=="all" || !clientids.empty()) )
	{
		getLastActs(helper, ret, clientids);
	}
	
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY