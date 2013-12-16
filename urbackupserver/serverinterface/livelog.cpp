#include "action_header.h"
#include "../server_log.h"
#include <algorithm>

namespace
{
	const unsigned int max_wait_time=10000;

	void wait_for_new_data(Helper &helper, int clientid, size_t lastid, std::vector<SCircularLogEntry>& entries)
	{
		unsigned int starttime=Server->getTimeMS();

		do
		{
			entries=ServerLogger::getCircularLogdata(clientid, lastid);

			if(!entries.empty())
				return;

			helper.sleep(500);
		}
		while(Server->getTimeMS()-starttime<max_wait_time);
	}

	void wait_for_new_data(Helper &helper, size_t lastid, std::vector<SCircularLogEntry>& entries)
	{
		unsigned int starttime=Server->getTimeMS();

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
	Helper helper(tid, &GET, &PARAMS);

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;

	int clientid=0;
	std::wstring s_clientid=GET[L"clientid"];
	if(!s_clientid.empty())
	{
		clientid=watoi(s_clientid);
	}

	std::wstring s_lastid=GET[L"lastid"];
	size_t lastid=std::string::npos;
	if(!s_lastid.empty())
	{
		lastid=watoi(s_lastid);
	}

	std::vector<SCircularLogEntry> logdata;

	std::vector<int> right_ids;
	if(clientid==0 && helper.getRights("logs")=="all")
	{
		wait_for_new_data(helper, lastid, logdata);
	}
	else if(clientid!=0 && helper.hasRights(clientid, helper.getRights("logs"), right_ids) )
	{
		wait_for_new_data(helper, clientid, lastid, logdata);
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

	helper.Write(ret.get(false));
}