/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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
#include "../server_status.h"

void getLastActs(Helper &helper, JSON::Object &ret, std::vector<int> clientids);

ACTION_IMPL(progress)
{
	Helper helper(tid, &POST, &PARAMS);
	JSON::Object ret;

	bool all_progress_rights = false;
	std::vector<int> progress_clientids = helper.clientRights("progress", all_progress_rights);

	bool all_stop_rights = false;
	std::vector<int> stop_clientids = helper.clientRights("stop_backup", all_stop_rights);

	bool all_log_rights = false;
	std::vector<int> log_clientids = helper.clientRights("logs", all_log_rights);


	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL && (all_progress_rights || !progress_clientids.empty()) )
	{
		if(POST.find("stop_clientid")!=POST.end() &&
			POST.find("stop_id")!=POST.end())
		{
			int stop_clientid=watoi(POST["stop_clientid"]);
			int stop_id=watoi(POST["stop_id"]);

			if(all_stop_rights
				|| std::find(stop_clientids.begin(), stop_clientids.end(), stop_clientid)!=stop_clientids.end())
			{
				IDatabase *db=helper.getDatabase();
				IQuery *q_get_name=db->Prepare("SELECT name FROM clients WHERE id=?");
				q_get_name->Bind(stop_clientid);
				db_results res=q_get_name->Read();
				if(!res.empty())
				{
					ServerStatus::stopProcess(res[0]["name"], stop_id, true);
				}
			}
		}

		JSON::Array pg;
		std::vector<SStatus> clients=ServerStatus::getStatus();
		for(size_t i=0;i<clients.size();++i)
		{
			int curr_clientid = clients[i].clientid;
			if(all_progress_rights
				|| std::find(progress_clientids.begin(), progress_clientids.end(), curr_clientid)!= progress_clientids.end() )
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
					obj.set("logid", JSON::Value(clients[i].processes[j].logid.first));
					obj.set("details", clients[i].processes[j].details);

					int64 add_time = Server->getTimeMS() - clients[i].processes[j].eta_set_time;
					int64 etams = clients[i].processes[j].eta_ms - add_time;
					if (etams>0 && etams<60 * 1000)
					{
						etams = 61 * 1000;
					}

					obj.set("eta_ms", etams);
					obj.set("speed_bpms", clients[i].processes[j].speed_bpms);

					if (clients[i].processes[j].can_stop 
						&& (all_stop_rights
							|| std::find(stop_clientids.begin(), stop_clientids.end(), curr_clientid) != stop_clientids.end() ) )
					{
						obj.set("can_stop_backup", true);
					}

					if (clients[i].processes[j].logid!=logid_t()
						&& (all_log_rights
							|| std::find(log_clientids.begin(), log_clientids.end(), curr_clientid) != log_clientids.end() ) )
					{
						obj.set("can_show_backup_log", true);
					}				

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

	if (POST["with_lastacts"] != "0")
	{
		bool all_lastacts_rights = false;
		std::vector<int> lastacts_clientids = helper.clientRights("lastacts", all_lastacts_rights);

		if (session != NULL && (all_lastacts_rights || !lastacts_clientids.empty()))
		{
			getLastActs(helper, ret, lastacts_clientids);
		}
	}	
	
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
