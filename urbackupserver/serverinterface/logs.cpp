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
#include "../../urbackupcommon/os_functions.h"
#include "../../urlplugin/IUrlFactory.h"

extern IUrlFactory *url_fak;

std::string constructFilter(const std::vector<int> &clientid, std::string key);

ACTION_IMPL(logs)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	std::wstring filter=GET[L"filter"];
	std::wstring s_logid=GET[L"logid"];
	int logid=watoi(s_logid);
	std::string rights=helper.getRights("logs");
	std::vector<int> clientid;
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientid.push_back(atoi(s_clientid[i].c_str()));
		}
	}
	if(clientid.size()>0 && filter.empty() )
	{
		for(size_t i=0;i<clientid.size();++i)
		{
			filter+=convert(clientid[i]);
			if(i+1<clientid.size())
				filter+=L",";
		}
	}
	std::vector<int> v_filter;
	if(!filter.empty())
	{
		std::vector<std::wstring> s_filter;
		Tokenize(filter, s_filter, L",");
		for(size_t i=0;i<s_filter.size();++i)
		{
			v_filter.push_back(watoi(s_filter[i]));
		}
	}
	if(session!=NULL && rights!="none")
	{
		IDatabase *db=helper.getDatabase();
		std::string qstr="SELECT c.id AS id, c.name AS name FROM clients c WHERE ";
		if(!clientid.empty()) qstr+=constructFilter(clientid, "c.id")+" AND ";
		qstr+=" EXISTS (SELECT id FROM logs l WHERE l.clientid=c.id LIMIT 1) ORDER BY name";
		
		IQuery *q_clients=db->Prepare(qstr);
		db_results res=q_clients->Read();
		q_clients->Reset();
		JSON::Array clients;
		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object obj;
			obj.set("id", watoi(res[i][L"id"]));
			obj.set("name", res[i][L"name"]);
			clients.add(obj);
		}
		if(clientid.empty())
		{
			ret.set("all_clients", JSON::Value(true));
		}
		ret.set("clients", clients);
		ret.set("has_user", session->id>0);

		IQuery *q_log_right_clients=db->Prepare("SELECT id, name FROM clients"+(clientid.empty()?""
											:" WHERE "+constructFilter(clientid, "id"))+" ORDER BY name");

		res=q_log_right_clients->Read();
		q_log_right_clients->Reset();
		JSON::Array log_right_clients;
		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object obj;
			obj.set("id", watoi(res[i][L"id"]));
			obj.set("name", res[i][L"name"]);
			log_right_clients.add(obj);
		}
		ret.set("log_right_clients", log_right_clients);

		ret.set("filter", filter);
		if(s_logid.empty())
		{
			std::wstring s_ll=GET[L"ll"];
			int ll=2;
			if(!s_ll.empty())
			{
				ll=watoi(s_ll);
			}
			std::string bed;
			if(ll==2)
			{
				bed="l.errors>0";
			}
			else if(ll==1)
			{
				bed="(l.warnings>0 OR l.errors>0)";
			}
			qstr="SELECT l.id AS id, c.name AS name, strftime('"+helper.getTimeFormatString()+"', l.created, 'localtime') AS time, l.errors AS errors, l.warnings AS warnings, "
				"l.image AS image, l.incremental AS incremental, l.resumed AS resumed FROM logs l INNER JOIN clients c ON l.clientid=c.id";

			if(!v_filter.empty())
			{
				qstr+=" WHERE "+constructFilter(v_filter, "l.clientid");
				if(!bed.empty())
				{
					qstr+=" AND "+bed;
				}
			}
			else if(!bed.empty()) qstr+=" WHERE "+bed;

			qstr+=" ORDER BY l.created DESC LIMIT 50";
			IQuery *q=db->Prepare(qstr);
			res=q->Read();
			q->Reset();
			JSON::Array logs;
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object obj;
				obj.set("name", res[i][L"name"]);
				obj.set("id", watoi(res[i][L"id"]));
				obj.set("time", res[i][L"time"]);
				obj.set("errors", watoi(res[i][L"errors"]));
				obj.set("warnings", watoi(res[i][L"warnings"]));
				obj.set("image", watoi(res[i][L"image"]));
				obj.set("incremental", watoi(res[i][L"incremental"]));
				obj.set("resumed", watoi(res[i][L"resumed"]));
				logs.add(obj);
			}
			ret.set("logs", logs);
			ret.set("ll", ll);

			if(GET.find(L"report_mail")!=GET.end())
			{
				IQuery *q=db->Prepare("UPDATE settings_db.si_users SET report_mail=?, report_loglevel=? WHERE id=?");
				q->Bind(GET[L"report_mail"]);
				q->Bind(watoi(GET[L"report_loglevel"]));
				q->Bind(session->id);
				q->Write();
				q->Reset();
			}

			if(GET.find(L"report_mail")!=GET.end())
			{
				IQuery *q=db->Prepare("UPDATE settings_db.si_users SET report_mail=?, report_loglevel=?, report_sendonly=? WHERE id=?");
				q->Bind(GET[L"report_mail"]);
				q->Bind(watoi(GET[L"report_loglevel"]));
				q->Bind(watoi(GET[L"report_sendonly"]));
				q->Bind(session->id);
				q->Write();
				ret.set("saved_ok", true);
			}

			IQuery *mq=db->Prepare("SELECT report_mail, report_loglevel, report_sendonly FROM settings_db.si_users WHERE id=? AND report_mail IS NOT NULL");
			mq->Bind(session->id);
			res=mq->Read();
			mq->Reset();
			
			if(!res.empty())
			{
				ret.set("report_mail", res[0][L"report_mail"]);
				ret.set("report_loglevel", watoi(res[0][L"report_loglevel"]));
				ret.set("report_sendonly", watoi(res[0][L"report_sendonly"]));
			}
			else
			{
				ret.set("report_mail", "");
				ret.set("report_sendonly", "");
				ret.set("report_loglevel", "");
			}

			if(url_fak!=NULL)
			{
				ret.set("HAS_MAIL_START", "");
				ret.set("HAS_MAIL_STOP", "");
			}
			else
			{
				ret.set("HAS_MAIL_START", "<!--");
				ret.set("HAS_MAIL_STOP", "-->");
			}
		}
		else
		{
			IQuery *q=db->Prepare("SELECT l.clientid AS clientid, ld.data AS logdata, strftime('"+helper.getTimeFormatString()+"', l.created, 'localtime') AS time, c.name AS name "
				"FROM ((logs l INNER JOIN log_data ld ON l.id=ld.logid) INNER JOIN clients c ON l.clientid=c.id) WHERE l.id=?");
			q->Bind(logid);
			db_results res=q->Read();
			q->Reset();
			
			if(!res.empty())
			{
				bool ok=true;
				if(!clientid.empty())
				{
					ok=false;
					int t_clientid=watoi(res[0][L"clientid"]);
					for(size_t i=0;i<clientid.size();++i)
					{
						if(clientid[i]==t_clientid)
						{
							ok=true;
							break;
						}
					}
				}
				
				if(ok)
				{
					JSON::Object log;
					log.set("data", res[0][L"logdata"]);
					log.set("time", res[0][L"time"]);
					log.set("clientname", res[0][L"name"]);
					ret.set("log", log);
				}
			}
		}
	}
	else
	{
		ret.set("error", 1);
	}

	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY