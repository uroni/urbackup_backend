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
#include "../server_settings.h"
#include "../os_functions.h"
#include "../server_status.h"

ACTION_IMPL(status)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;

	std::string rights=helper.getRights("status");
	std::vector<int> clientids;
	IDatabase *db=helper.getDatabase();
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientids.push_back(atoi(s_clientid[i].c_str()));
		}
	}

	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	if(session!=NULL && (rights=="all" || !clientids.empty()) )
	{
		{
			ServerSettings settings(db);
			if(!os_directory_exists(settings.getSettings()->backupfolder) || !os_directory_exists(settings.getSettings()->backupfolder_uncompr) || settings.getSettings()->backupfolder.empty())
			{
				ret.set("dir_error", true);
			}
			else if(!os_directory_exists(settings.getSettings()->backupfolder+os_file_sep()+L"clients") && !os_create_dir(settings.getSettings()->backupfolder+os_file_sep()+L"clients") )
			{
				ret.set("dir_error" ,true);
			}
		}

		bool details=false;
		if(GET.find(L"details")!=GET.end())
		{
			details=true;
			ret.set("details", true);
		}
		std::wstring hostname=GET[L"hostname"];
		if(!hostname.empty())
		{
			if(GET[L"remove"]==L"true")
			{
				IQuery *q=db->Prepare("DELETE FROM extra_clients WHERE id=?");
				q->Bind(hostname);
				q->Write();
				q->Reset();
			}
			else
			{
				IQuery *q=db->Prepare("INSERT INTO extra_clients (hostname) SELECT ? AS hostname WHERE NOT EXISTS (SELECT hostname FROM extra_clients WHERE hostname=?)");
				q->Bind(hostname);
				q->Bind(hostname);
				q->Write();
				q->Reset();
			}
		}

		JSON::Array status;
		IDatabase *db=helper.getDatabase();
		std::string filter;
		if(!clientids.empty())
		{
			filter=" WHERE ";
			for(size_t i=0;i<clientids.size();++i)
			{
				filter+="id="+nconvert(clientids[i]);
				if(i+1<clientids.size())
					filter+=" OR ";
			}
		}
		db_results res=db->Read("SELECT id, name, strftime('"+helper.getTimeFormatString()+"', lastbackup, 'localtime') AS lastbackup, strftime('"+helper.getTimeFormatString()+"', lastseen, 'localtime') AS lastseen,"
			"strftime('"+helper.getTimeFormatString()+"', lastbackup_image, 'localtime') AS lastbackup_image FROM clients"+filter);

		int backup_ok_mod=3;
		db_results res_t=db->Read("SELECT value FROM settings WHERE key='backup_ok_mod' AND clientid=0");
		if(res_t.size()>0)
		{
			backup_ok_mod=watoi(res_t[0][L"value"]);
		}

		std::vector<SStatus> client_status=ServerStatus::getStatus();

		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object stat;
			int clientid=watoi(res[i][L"id"]);
			std::wstring clientname=res[i][L"name"];
			stat.set("id", clientid);
			stat.set("name", clientname);
			stat.set("lastbackup", res[i][L"lastbackup"]);
			stat.set("lastseen", res[i][L"lastseen"]);
			stat.set("lastbackup_image", res[i][L"lastbackup_image"]);

			std::string ip="-";
			int i_status=0;
			bool online=false;
			for(size_t j=0;j<client_status.size();++j)
			{
				if(client_status[j].client==wnarrow(clientname))
				{
					if(client_status[j].r_online==true)
					{
						online=true;
					}
					unsigned char *ips=(unsigned char*)&client_status[j].ip_addr;
					ip=nconvert(ips[0])+"."+nconvert(ips[1])+"."+nconvert(ips[2])+"."+nconvert(ips[3]);

					i_status=client_status[j].statusaction;
				}
			}

			stat.set("online", online);
			stat.set("ip", ip);
			stat.set("status", i_status);
			

			ServerSettings settings(db, clientid);
			IQuery *q=db->Prepare("SELECT id FROM clients WHERE lastbackup IS NOT NULL AND datetime('now','-"+nconvert(settings.getSettings()->update_freq_incr*backup_ok_mod)+" seconds')<lastbackup AND id=?");
			q->Bind(clientid);
			db_results res_file_ok=q->Read();
			q->Reset();
			stat.set("file_ok", !res_file_ok.empty());

			q=db->Prepare("SELECT id FROM clients WHERE lastbackup_image IS NOT NULL AND datetime('now','-"+nconvert(settings.getSettings()->update_freq_image_incr*backup_ok_mod)+" seconds')<lastbackup_image AND id=?");
			q->Bind(clientid);
			res_file_ok=q->Read();
			q->Reset();
			stat.set("image_ok", !res_file_ok.empty());

			status.add(stat);
		}

		for(size_t i=0;i<client_status.size();++i)
		{
			bool found=false;
			for(size_t j=0;j<res.size();++j)
			{
				if(wnarrow(res[j][L"name"])==client_status[i].client)
				{
					found=true;
					break;
				}
			}

			if(found) continue;

			JSON::Object stat;
			stat.set("id", "-");
			stat.set("name", client_status[i].client);
			stat.set("lastbackup", "-");
			stat.set("lastseen", "-");
			stat.set("lastbackup_image", "-");
			stat.set("online", client_status[i].r_online);
			std::string ip;
			unsigned char *ips=(unsigned char*)&client_status[i].ip_addr;
			ip=nconvert(ips[0])+"."+nconvert(ips[1])+"."+nconvert(ips[2])+"."+nconvert(ips[3]);
			stat.set("ip", ip);

			if(client_status[i].wrong_ident)
				stat.set("status", 11);
			else
				stat.set("status", 10);

			stat.set("file_ok", false);
			stat.set("image_ok", false);

			status.add(stat);
		}
		JSON::Array extra_clients;

		res=db->Read("SELECT id, hostname, lastip FROM extra_clients");
		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object extra_client;

			extra_client.set("hostname", res[i][L"hostname"]);

			_i64 i_ip=os_atoi64(wnarrow(res[i][L"lastip"]));

			bool online=false;

			for(size_t j=0;j<client_status.size();++j)
			{
				if(i_ip==(_i64)client_status[j].ip_addr)
				{
					online=true;
				}
			}
			extra_client.set("id", res[i][L"id"]);
			extra_client.set("online", online);

			extra_clients.add(extra_client);
		}

		ret.set("status", status);
		ret.set("extra_clients", extra_clients);

	}
	else
	{
		ret.set("error", 1);
	}
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY