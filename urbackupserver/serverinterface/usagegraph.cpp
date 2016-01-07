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

namespace
{
	struct SUsed
	{
		SUsed(void):used(0) {}
		SUsed(const std::string &pTdate_full, const std::string &pTdate, double pUsed): tdate_full(pTdate_full), tdate(pTdate), used(pUsed) {}
		std::string tdate_full;
		std::string tdate;
		double used;
	};
}

ACTION_IMPL(usagegraph)
{
	Helper helper(tid, &POST, &PARAMS);

	IDatabase *db=helper.getDatabase();
	int clientid=-1;
	std::string s_clientid=POST["clientid"];
	if(!s_clientid.empty())
	{
		IQuery *q=db->Prepare("SELECT id,name FROM clients WHERE id=?");
		q->Bind(s_clientid);
		db_results res=q->Read();
		q->Reset();

		if(!res.empty())
		{
			clientid=watoi(res[0]["id"]);
		}
	}

	std::string rights=helper.getRights("piegraph");
	bool client_id_found=false;
	if(rights!="all" && rights!="none" )
	{
		std::vector<int> v_clientid;
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			v_clientid.push_back(atoi(s_clientid[i].c_str()));
		}
		if(clientid!=-1)
		{
			for(size_t i=0;i<v_clientid.size();++i)
			{
				if(clientid==v_clientid[i])
				{
					client_id_found=true;
					break;
				}
			}
		}
	}	

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL &&
		( ( clientid==-1 && helper.getRights("piegraph")=="all" ) || ( clientid!=-1 && (client_id_found || rights=="all") ) )
	  )
	{	
		helper.releaseAll();
		
		std::string scale = POST["scale"];

		if(scale.empty())
		{
			scale="d";
		}

		std::string t_where=" 0=0";
		if(clientid!=-1)
		{
			t_where+=" AND id="+convert(clientid)+" ";
		}
		
		int c_lim=1;
		if(clientid==-1)
		{
			IQuery *q_count_clients=db->Prepare("SELECT count(*) AS c FROM clients");
			db_results res_c=q_count_clients->Read();
			q_count_clients->Reset();
			if(!res_c.empty())
				c_lim=watoi(res_c[0]["c"]);
		}

		unsigned int n_items;
		std::string back;
		std::string date_fmt;
		if(scale=="y")
		{
			back="-40 year";
			n_items=40;
			date_fmt="%Y";
		}
		else if(scale=="m")
		{
			back="-3 year";
			n_items=40;
			date_fmt="%Y-%m";
		}
		else
		{
			back="-2 month";
			n_items=40;
			date_fmt="%Y-%m-%d";
		}

		
		IQuery *q=db->Prepare("SELECT id, (MAX(b.bytes_used_files)+MAX(b.bytes_used_images)) AS used, strftime('"+date_fmt+"', MAX(b.created), 'localtime') AS tdate, strftime('%Y-%m-%d', MAX(b.created), 'localtime') AS tdate_full "
				    "FROM "
				    "("
				    "SELECT MAX(created) AS mcreated "
					"FROM "
					"(SELECT * FROM clients_hist WHERE "+t_where+" AND created>date('now','"+back+"') ) "
				    "GROUP BY strftime('"+date_fmt+"', created, 'localtime'), id "
					"HAVING mcreated>date('now','"+back+"') "
				    "ORDER BY mcreated DESC "
				    "LIMIT "+convert(c_lim*(n_items*2))+" "
				    ") a "
				    "INNER JOIN clients_hist b ON a.mcreated=b.created WHERE "+t_where+" AND b.created>date('now','"+back+"') "
				    "GROUP BY strftime('"+date_fmt+"', b.created, 'localtime'), id "
				    "ORDER BY b.created DESC");
				    
		db_results res=q->Read();
		std::vector<SUsed > used;
		for(size_t i=0;i<res.size();++i)
		{
			bool found=false;
			for(size_t j=0;j<used.size();++j)
			{
				if(used[j].tdate==res[i]["tdate"])
				{
					found=true;
					used[j].used+=atof(res[i]["used"].c_str());
				}
			}
			if(!found && used.size()<n_items)
			{
				used.push_back(SUsed(res[i]["tdate_full"], res[i]["tdate"], atof(res[i]["used"].c_str()) ) );
			}
		}

		bool size_gb=false;
		for(size_t i=0;i<used.size();++i)
		{
			if(used[i].used>1024*1024*1024)
				size_gb=true;
		}

		JSON::Array data;
		for(int i=(int)used.size()-1;i>=0;--i)
		{
			double size=used[i].used;
			if(size_gb)
			{
				size/=1024*1024*1024;
			}
			else
			{
				size/=1024*1024;
			}
			JSON::Object obj;
			obj.set("xlabel", (used[i].tdate_full));
			obj.set("data", (float)size);
			data.add(obj);
		}

		ret.set("data", data);

		if(size_gb)
			ret.set("ylabel", "GB");
		else
			ret.set("ylabel", "MB");

		helper.update(tid, &POST, &PARAMS);
	}
	else
	{
		ret.set("error", 1);
	}
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
