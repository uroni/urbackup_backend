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

#include "action_header.h"
#include "../server_log.h"
#include <algorithm>

namespace
{
	const unsigned int max_wait_time=10000;

	void wait_for_new_data(Helper &helper, int clientid, size_t lastid, std::vector<SCircularLogEntry>& entries, logid_t logid)
	{
		int64 starttime=Server->getTimeMS();

		do
		{
			entries=ServerLogger::getCircularLogdata(clientid, lastid, logid);

			if(!entries.empty())
				return;

			helper.sleep(500);
		}
		while(Server->getTimeMS()-starttime<max_wait_time);
	}

	void wait_for_new_data(Helper &helper, size_t lastid, std::vector<SCircularLogEntry>& entries)
	{
		int64 starttime=Server->getTimeMS();

		do
		{
			entries=Server->getCicularLogBuffer(lastid);

			if(!entries.empty())
				return;

			helper.sleep(500);
		}
		while(Server->getTimeMS()-starttime<max_wait_time);
	}

	struct LogEntryTimeSort
	{
		bool operator()(const SCircularLogEntry& e1, const SCircularLogEntry& e2) const
		{
			return e1.time<e2.time;
		}
	};

	void time_sort(std::vector<SCircularLogEntry>& input)
	{
		LogEntryTimeSort timesort;
		std::stable_sort(input.begin(), input.end(), timesort);
	}
}

ACTION_IMPL(livelog)
{
	Helper helper(tid, &POST, &PARAMS);

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session==NULL)
	{
		JSON::Object ret;
		ret.set("error", true);
        helper.Write(ret.stringify(false));
		return;
	}

	int clientid=0;
	std::string s_clientid=POST["clientid"];
	if(!s_clientid.empty())
	{
		clientid=watoi(s_clientid);
	}

	std::string s_lastid=POST["lastid"];
	size_t lastid=std::string::npos;
	if(!s_lastid.empty())
	{
		lastid=watoi(s_lastid);
	}

	logid_t logid = logid_t();
	str_map::iterator logid_it = POST.find("logid");
	if (logid_it != POST.end())
	{
		logid.first = watoi64(logid_it->second);
	}

	std::vector<SCircularLogEntry> logdata;

	bool all_log_rights = false;
	std::vector<int> right_ids = helper.clientRights("logs", all_log_rights);
	if(clientid==0 && all_log_rights)
	{
		wait_for_new_data(helper, lastid, logdata);
	}
	else if(clientid!=0 
		&& (all_log_rights 
			|| std::find(right_ids.begin(), right_ids.end(), clientid)!=right_ids.end() ) )
	{
		wait_for_new_data(helper, clientid, lastid, logdata, logid);
	}

	time_sort(logdata);

	JSON::Object ret;
	JSON::Array j_logdata;

	for(size_t i=0;i<logdata.size();++i)
	{
		const SCircularLogEntry& entry=logdata[i];
		if(entry.id!=std::string::npos && (lastid==std::string::npos || entry.id>lastid) )
		{
			JSON::Object obj;
			obj.set("msg", entry.utf8_msg);
			obj.set("id", entry.id);
			obj.set("loglevel", entry.loglevel);
			obj.set("time", entry.time);

			j_logdata.add(obj);
		}
	}

	ret.set("logdata", j_logdata);

    helper.Write(ret.stringify(false));
}
