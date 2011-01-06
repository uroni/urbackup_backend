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

const unsigned int n_items=30;

struct SUsed
{
	SUsed(void):used(0) {}
	SUsed(const std::wstring &pTdate_short, const std::wstring &pTdate, double pUsed): tdate_short(pTdate_short), tdate(pTdate), used(pUsed) {}
	std::wstring tdate_short;
	std::wstring tdate;
	double used;
};

ACTION_IMPL(usagegraph)
{
	Helper helper(tid, &GET, &PARAMS);

	IDatabase *db=helper.getDatabase();
	int clientid=-1;
	std::wstring s_clientid=GET[L"clientid"];
	if(!s_clientid.empty())
	{
		IQuery *q=db->Prepare("SELECT id,name FROM clients WHERE id=?");
		q->Bind(s_clientid);
		db_results res=q->Read();
		q->Reset();

		if(!res.empty())
		{
			clientid=watoi(res[0][L"id"]);
		}
	}

	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL &&
		( ( clientid==-1 && helper.getRights("piegraph")=="all" ) || ( clientid!=-1 && helper.getRights("piegraph_"+nconvert(clientid))=="all" ) )
	  )
	{	
		helper.releaseAll();

		std::wstring scale=GET[L"scale"];

		std::string t_where="";
		if(clientid!=-1)
		{
			t_where=" WHERE id="+nconvert(clientid)+" ";
		}
		
		int c_lim=1;
		if(clientid==-1)
		{
			IQuery *q_count_clients=db->Prepare("SELECT count(*) AS c FROM clients");
			db_results res_c=q_count_clients->Read();
			q_count_clients->Reset();
			if(!res_c.empty())
				c_lim=watoi(res_c[0][L"c"]);
		}
			
		//IQuery *q=db->Prepare("SELECT strftime('"+date_format_str+"', created, 'localtime') AS tdate, strftime('"+date_format_str_short+"', created, 'localtime') AS tdate_short, (MAX(bytes_used_files)+MAX(bytes_used_images)) AS used , id FROM clients_hist "+t_where+" GROUP BY strftime('"+date_format_str+"', created, 'localtime'), id ORDER BY created DESC LIMIT "+nconvert(c_lim*(n_items*2)));
		IQuery *q=db->Prepare("SELECT id, (MAX(b.bytes_used_files)+MAX(b.bytes_used_images)) AS used, strftime('"+date_format_str+"', MAX(b.created), 'localtime') AS tdate, strftime('"+date_format_str_short+"', MAX(b.created), 'localtime') AS tdate_short "
				    "FROM "
				    "("
				    "SELECT MAX(created) AS created FROM clients_hist"+t_where+" "
				    "GROUP BY strftime('"+date_format_str+"', created, 'localtime'), id "
				    "ORDER BY created DESC "
				    "LIMIT "+nconvert(c_lim*(n_items*2))+" "
				    ") a "
				    "INNER JOIN clients_hist b ON a.created=b.created"+t_where+" "
				    "GROUP BY strftime('"+date_format_str+"', b.created, 'localtime'), id "
				    "ORDER BY b.created DESC");
				    
		db_results res=q->Read();
		std::vector<SUsed > used;
		for(size_t i=0;i<res.size();++i)
		{
			bool found=false;
			for(size_t j=0;j<used.size();++j)
			{
				if(used[j].tdate==res[i][L"tdate"])
				{
					found=true;
					used[j].used+=atof(wnarrow(res[i][L"used"]).c_str());
				}
			}
			if(!found && used.size()<n_items)
			{
				used.push_back(SUsed(res[i][L"tdate_short"], res[i][L"tdate"], atof(wnarrow(res[i][L"used"]).c_str()) ) );
			}
		}

		bool size_gb=false;
		for(size_t i=0;i<used.size();++i)
		{
			if(used[i].used>1024*1024*1024)
				size_gb=true;
		}

		SBarInfo bi;
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
			bi.xlabels.push_back(Server->ConvertToUTF8(used[i].tdate_short));
			bi.data.push_back((float)size);
		}
		bi.title="";
		if(size_gb)
			bi.ylabel="GB";
		else
			bi.ylabel="MB";

		bi.sizex=800;
		bi.sizey=500;
		bi.barwidth=1.f;

		IPychart *pychart=pychart_fak->getPychart();
		unsigned int id=pychart->drawBar(bi);
		session->mInt[L"image_"+convert(id)]=1;
		ret.set("image_id", id);
	}
	else
	{
		ret.set("error", 1);
	}
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY