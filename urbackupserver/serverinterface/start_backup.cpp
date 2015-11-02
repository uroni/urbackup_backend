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

#include "action_header.h"
#include "../../Interface/Pipe.h"
#include "../server_status.h"
#include <algorithm>

namespace
{
	bool client_start_backup(IPipe *comm_pipe, std::wstring backup_type)
	{
		if(backup_type==L"full_file")
			comm_pipe->Write("START BACKUP FULL");
		else if(backup_type==L"incr_file")
			comm_pipe->Write("START BACKUP INCR");
		else if(backup_type==L"full_image")
			comm_pipe->Write("START IMAGE FULL");
		else if(backup_type==L"incr_image")
			comm_pipe->Write("START IMAGE INCR");
		else
			return false;

		return true;
	}
}

ACTION_IMPL(start_backup)
{
	Helper helper(tid, &GET, &PARAMS);

	std::string status_rights=helper.getRights("status");
	std::vector<int> status_right_clientids;
	IDatabase *db=helper.getDatabase();
	if(status_rights!="all" && status_rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(status_rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			status_right_clientids.push_back(atoi(s_clientid[i].c_str()));
		}
	}

	std::wstring s_start_client=GET[L"start_client"];
	std::vector<int> start_client;
	std::wstring start_type=GET[L"start_type"];

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;

	if(session!=NULL && !s_start_client.empty() && helper.getRights("start_backup")=="all")
	{
		std::vector<SStatus> client_status=ServerStatus::getStatus();

		std::vector<std::wstring> sv_start_client;
		Tokenize(s_start_client, sv_start_client, L",");

		JSON::Array result;

		for(size_t i=0;i<sv_start_client.size();++i)
		{
			int start_clientid = watoi(sv_start_client[i]);

			if( status_rights!="all"
				&& std::find(status_right_clientids.begin(), status_right_clientids.end(),
							 start_clientid)==status_right_clientids.end())
			{
				continue;
			}

			JSON::Object obj;

			obj.set("start_type", start_type);
			obj.set("clientid", start_clientid);

			bool found_client=false;
			for(size_t i=0;i<client_status.size();++i)
			{
				if(client_status[i].clientid==start_clientid)
				{
					found_client=true;
					
					if(!client_status[i].r_online || client_status[i].comm_pipe==NULL)
					{
						obj.set("start_ok", false);
					}
					else
					{
						if(client_start_backup(client_status[i].comm_pipe, start_type) )
						{
							obj.set("start_ok", true);
						}
						else
						{
							obj.set("start_ok", false);
						}
					}

					break;
				}
			}

			if(!found_client)
			{
				obj.set("start_ok", false);
			}

			result.add(obj);
		}

		JSON::Object ret;
		ret.set("result", result);
        helper.Write(ret.stringify(false));
	}
	else
	{
		JSON::Object ret;
		ret.set("error", 1);
	}
}
