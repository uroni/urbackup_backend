#include "action_header.h"
#include "../server_log.h"
#include <algorithm>

namespace
{
	const unsigned int max_wait_time=10000;

	const std::vector<SCircularLogEntry>* wait_for_new_data(int clientid, size_t lastid)
	{
		unsigned int starttime=Server->getTimeMS();

		const std::vector<SCircularLogEntry>* logdata;

		do
		{
			logdata=&ServerLogger::getCircularLogdata(clientid);

			if(lastid==std::string::npos)
				return logdata;

			for(size_t i=0;i<logdata->size();++i)
			{
				if((*logdata)[i].id>lastid && (*logdata)[i].id!=std::string::npos)
				{
					return logdata;
				}
			}

			Server->wait(500);
		}
		while(Server->getTimeMS()-starttime<max_wait_time);

		return logdata;
	}

	const std::vector<SCircularLogEntry>* wait_for_new_data(size_t lastid)
	{
		unsigned int starttime=Server->getTimeMS();

		const std::vector<SCircularLogEntry>* logdata;

		do
		{
			logdata=&Server->getCicularLogBuffer();

			if(lastid==std::string::npos)
				return logdata;

			for(size_t i=0;i<logdata->size();++i)
			{
				if((*logdata)[i].id>lastid && (*logdata)[i].id!=std::string::npos)
				{
					return logdata;
				}
			}

			Server->wait(500);
		}
		while(Server->getTimeMS()-starttime<max_wait_time);

		return logdata;
	}

	struct LogEntryTimeSort
	{
		bool operator()(const SCircularLogEntry& e1, const SCircularLogEntry& e2) const
		{
			return e1.time<e2.time;
		}
	};

	std::vector<SCircularLogEntry> time_sort(const std::vector<SCircularLogEntry>* input)
	{
		std::vector<SCircularLogEntry> sort_copy=*input;
		LogEntryTimeSort timesort;
		std::stable_sort(sort_copy.begin(), sort_copy.end(), timesort);

		return sort_copy;
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

	const std::vector<SCircularLogEntry>* logdata=NULL;

	std::vector<int> right_ids;
	if(clientid==0 && helper.getRights("logs")=="all")
	{
		logdata=wait_for_new_data(lastid);
	}
	else if(clientid!=0 && helper.hasRights(clientid, helper.getRights("logs"), right_ids) )
	{
		logdata=wait_for_new_data(clientid, lastid);
	}

	if(logdata==NULL)
	{
		return;
	}

	std::vector<SCircularLogEntry> sorted_logdata=time_sort(logdata);

	JSON::Object ret;
	JSON::Array j_logdata;

	for(size_t i=0;i<sorted_logdata.size();++i)
	{
		const SCircularLogEntry& entry=sorted_logdata[i];
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