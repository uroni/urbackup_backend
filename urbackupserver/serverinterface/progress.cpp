/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL && (rights=="all" || !clientids.empty()) )
	{
		if(GET.find(L"stop_clientid")!=GET.end() &&
			GET.find(L"stop_id")!=GET.end())
		{
			int stop_clientid=watoi(GET[L"stop_clientid"]);
			int stop_id=watoi(GET[L"stop_id"]);

			std::string stop_rights=helper.getRights("stop_backup");
			bool stop_ok=false;
			if(stop_rights=="all")
			{
				stop_ok=true;
			}
			else
			{
				std::vector<std::string> s_cid;
				Tokenize(stop_rights, s_cid, ",");
				for(size_t i=0;i<s_cid.size();++i)
				{
					if(atoi(s_cid[i].c_str())==stop_clientid)
					{
						stop_ok=true;
					}
				}
			}

			if(stop_ok)
			{
				IDatabase *db=helper.getDatabase();
				IQuery *q_get_name=db->Prepare("SELECT name FROM clients WHERE id=?");
				q_get_name->Bind(stop_clientid);
				db_results res=q_get_name->Read();
				if(!res.empty())
				{
					ServerStatus::stopProcess(res[0][L"name"], stop_id, true);
				}
			}
		}

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
			if(rights=="all" || found==true)
			{
				for(size_t j=0;j<clients[i].processes.size();++j)
				{
					JSON::Object obj;
					obj.set("name", JSON::Value(clients[i].client));
					obj.set("clientid", JSON::Value(clients[i].clientid));
					obj.set("action", JSON::Value(static_cast<int>(clients[i].processes[j].action)));
					obj.set("pcdone", JSON::Value(clients[i].processes[j].pcdone));
					obj.set("queue", JSON::Value(clients[i].processes[j].prepare_hashqueuesize+
						clients[i].processes[j].hashqueuesize));
					obj.set("id", JSON::Value(clients[i].processes[j].id));
					pg.add(obj);
				}
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