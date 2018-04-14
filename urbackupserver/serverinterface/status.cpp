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
#include "../server_settings.h"
#include "../../urbackupcommon/os_functions.h"
#include "../server_status.h"
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../server.h"
#include "../ClientMain.h"
#include "../dao/ServerBackupDao.h"

#include <algorithm>
#include <memory>

extern ICryptoFactory *crypto_fak;

namespace
{

bool client_download(Helper& helper, JSON::Array &client_downloads)
{
	IDatabase *db=helper.getDatabase();
	ServerSettings settings(db);

	if(!FileExists("urbackup/UrBackupUpdate.exe"))
		return false;

	if(!FileExists("urbackup/UrBackupUpdate.sig2"))
		return false;

	if(crypto_fak==NULL)
		return false;

	bool clientid_rights_all;
	std::vector<int> clientid_rights=helper.clientRights(RIGHT_SETTINGS, clientid_rights_all);

	db_results res=db->Read("SELECT id, name FROM clients ORDER BY name");

	bool has_client=false;

	for(size_t i=0;i<res.size();++i)
	{
		int clientid=watoi(res[i]["id"]);
		std::string clientname=res[i]["name"];

		bool found=false;
		if( (clientid_rights_all
			|| std::find(clientid_rights.begin(), clientid_rights.end(), clientid)!=clientid_rights.end()
			) /*&& ServerStatus::getStatus(clientname).online==false*/ )
		{
			JSON::Object obj;
			obj.set("id", clientid);
			obj.set("name", clientname);
			client_downloads.add(obj);
			has_client=true;
		}
	}

	return has_client;
}

std::string get_stop_show_version(IDatabase *db)
{
	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='stop_show_version'");
	if (!res.empty())
	{
		return res[0]["tvalue"];
	}
	return std::string();
}

void set_server_version_info(IDatabase* db, JSON::Object& ret)
{
	std::auto_ptr<ISettingsReader> infoProperties(Server->createFileSettingsReader("urbackup/server_version_info.properties"));

	if(infoProperties.get())
	{
		std::string stop_show_version = get_stop_show_version(db);
		std::string curr_version_str;
		if (infoProperties->getValue("curr_version_str", &curr_version_str))
		{
			if (stop_show_version != curr_version_str)
			{
				ret.set("curr_version_str", curr_version_str);

				std::string curr_version_num;
				if (infoProperties->getValue("curr_version_num", &curr_version_num))
				{
					ret.set("curr_version_num", watoi64(curr_version_num));
				}
			}
		}		
	}
}

void add_remove_stop_show(IDatabase* db, std::string stop_show, bool add)
{
	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='stop_show'");
	if (!res.empty())
	{
		std::vector<std::string> toks;
		Tokenize(res[0]["tvalue"], toks, ",");
		std::vector<std::string>::iterator it = std::find(toks.begin(), toks.end(), stop_show);
		if (add)
		{
			if (it == toks.end())
			{
				toks.push_back(stop_show);
			}
		}
		else
		{
			if (it != toks.end())
			{
				toks.erase(it);
			}
		}

		std::string nval;
		for (size_t i = 0; i < toks.size(); ++i)
		{
			if (!nval.empty()) nval += ",";
			nval += toks[i];
		}

		IQuery* q = db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='stop_show'");
		q->Bind(nval);
		q->Write();
		q->Reset();
	}
	else
	{
		IQuery* q = db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES ('stop_show', ?)");
		q->Bind(stop_show);
		q->Write();
		q->Reset();
	}
}

void set_stop_show_version(IDatabase* db, std::string ver)
{
	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='stop_show_version'");
	if (!res.empty())
	{
		IQuery* q = db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='stop_show_version'");
		q->Bind(ver);
		q->Write();
		q->Reset();
	}
	else
	{
		IQuery* q = db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES ('stop_show_version', ?)");
		q->Bind(ver);
		q->Write();
		q->Reset();
	}
}

bool is_stop_show(IDatabase* db, std::string stop_key)
{
	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='stop_show'");
	if (!res.empty())
	{
		std::vector<std::string> toks;
		Tokenize(res[0]["tvalue"], toks, ",");
		return std::find(toks.begin(), toks.end(), stop_key) != toks.end();
	}
	return false;
}

}

ACTION_IMPL(status)
{
	Helper helper(tid, &POST, &PARAMS);
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
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	if(session!=NULL && (rights=="all" || !clientids.empty()) )
	{
		if (rights == "all" && POST.find("stop_show") != POST.end())
		{
			add_remove_stop_show(db, POST["stop_show"], true);
		}

		if (rights == "all" && POST.find("stop_show_version") != POST.end())
		{
			set_stop_show_version(db, POST["stop_show_version"]);
		}

		if (rights == "all" && POST.find("reset_error") != POST.end())
		{
			std::string reset_error = POST["reset_error"];
			if (reset_error == "nospc_stalled")
			{
				ServerStatus::resetServerNospcStalled();
			}
			else if (reset_error == "nospc_fatal")
			{
				ServerStatus::setServerNospcFatal(false);
			}
			else if (reset_error == "database_error")
			{
				Server->clearFailBit(IServer::FAIL_DATABASE_CORRUPTED);
				Server->clearFailBit(IServer::FAIL_DATABASE_IOERR);
				Server->clearFailBit(IServer::FAIL_DATABASE_FULL);
			}
		}

		if(rights=="all")
		{
			ret.set("has_status_check", true);

			if(ServerStatus::getServerNospcStalled()>0)
			{
				ret.set("nospc_stalled" ,true);
			}
			if(ServerStatus::getServerNospcFatal())
			{
				ret.set("nospc_fatal" ,true);
			}

			if( (Server->getFailBits() & IServer::FAIL_DATABASE_CORRUPTED) ||
				(Server->getFailBits() & IServer::FAIL_DATABASE_IOERR) ||
				(Server->getFailBits() & IServer::FAIL_DATABASE_FULL) )
			{
				ret.set("database_error", true);
			}
		}

		std::string hostname=POST["hostname"];
		if(!hostname.empty() && rights=="all")
		{
			if(POST["remove"]=="true")
			{
				IQuery *q=db->Prepare("DELETE FROM settings_db.extra_clients WHERE id=?");
				q->Bind(hostname);
				q->Write();
				q->Reset();
			}
			else
			{
				IQuery *q=db->Prepare("INSERT INTO settings_db.extra_clients (hostname) SELECT ? AS hostname WHERE NOT EXISTS (SELECT hostname FROM settings_db.extra_clients WHERE hostname=?)");
				q->Bind(hostname);
				q->Bind(hostname);
				q->Write();
				q->Reset();
			}
		}
		
		std::string s_remove_client=POST["remove_client"];
		if(!s_remove_client.empty() && helper.getRights("remove_client")=="all")
		{
			std::vector<std::string> remove_client;
			Tokenize(s_remove_client, remove_client, ",");
			if(POST.find("stop_remove_client")!=POST.end())
			{
				for(size_t i=0;i<remove_client.size();++i)
				{
					IQuery *q=db->Prepare("UPDATE clients SET delete_pending=0 WHERE id=?");
					q->Bind(remove_client[i]);
					q->Write();
					q->Reset();
				}
			}
			else
			{
				for(size_t i=0;i<remove_client.size();++i)
				{
					IQuery *q=db->Prepare("UPDATE clients SET delete_pending=1 WHERE id=? OR virtualmain = (SELECT name FROM clients WHERE id=?)");
					q->Bind(remove_client[i]);
					q->Bind(remove_client[i]);
					q->Write();
					q->Reset();
				}
			}
			BackupServer::updateDeletePending();
		}

		JSON::Array status;
		IDatabase *db=helper.getDatabase();
		std::string filter;
		if(!clientids.empty())
		{
			filter=" WHERE ";
			for(size_t i=0;i<clientids.size();++i)
			{
				filter+="c.id="+convert(clientids[i]);
				if(i+1<clientids.size())
					filter+=" OR ";
			}
		}
		db_results res=db->Read("SELECT c.id AS id, delete_pending, c.name AS name, strftime('"+helper.getTimeFormatString()+"', lastbackup) AS lastbackup, strftime('"+helper.getTimeFormatString()+"', lastseen) AS lastseen,"
			"strftime('"+helper.getTimeFormatString()+"', lastbackup_image) AS lastbackup_image, last_filebackup_issues, os_simple, os_version_str, client_version_str, cg.name AS groupname, file_ok, image_ok FROM "
			" clients c LEFT OUTER JOIN settings_db.si_client_groups cg ON c.groupid = cg.id "+filter+" ORDER BY name");

		std::vector<SStatus> client_status=ServerStatus::getStatus();

		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object stat;
			int clientid=watoi(res[i]["id"]);
			std::string clientname=res[i]["name"];
			stat.set("id", clientid);
			stat.set("name", clientname);
			int64 lastbackup = watoi64(res[i]["lastbackup"]);
			int64 lastbackup_image = watoi64(res[i]["lastbackup_image"]);
			stat.set("lastbackup", lastbackup);
			stat.set("lastbackup_image", lastbackup_image);
			stat.set("delete_pending", res[i]["delete_pending"] );
			int issues = watoi(res[i]["last_filebackup_issues"]);
			if (issues == ServerBackupDao::num_issues_no_backuppaths)
			{
				stat.set("last_filebackup_issues", 0);
				stat.set("no_backup_paths", true);
			}
			else
			{
				stat.set("last_filebackup_issues", issues);
			}
			stat.set("groupname", res[i]["groupname"]);
			stat.set("file_ok", res[i]["file_ok"] == "1" && lastbackup!=0);
			stat.set("image_ok", res[i]["image_ok"] == "1" && lastbackup_image!=0);

			if (res[i]["file_ok"] == "-1")
			{
				stat.set("file_disabled", true);
			}

			if (res[i]["image_ok"] == "-1")
			{
				stat.set("image_disabled", true);
			}

			std::string ip="-";
			std::string client_version_string = res[i]["client_version_str"];
			std::string os_version_string = res[i]["os_version_str"];
			std::string os_simple = res[i]["os_simple"];
			int i_status=0;
			bool online=false;
			SStatus *curr_status=NULL;
			JSON::Array processes;
			int64 lastseen = watoi64(res[i]["lastseen"]);

			for(size_t j=0;j<client_status.size();++j)
			{
				if(client_status[j].client==clientname)
				{
					if(client_status[j].r_online==true)
					{
						curr_status=&client_status[j];
						online=true;
					}

					unsigned char *ips=(unsigned char*)&client_status[j].ip_addr;
					ip=convert(ips[0])+"."+convert(ips[1])+"."+convert(ips[2])+"."+convert(ips[3]);

					client_version_string=client_status[j].client_version_string;
					os_version_string=client_status[j].os_version_string;

					if (client_status[j].lastseen > lastseen)
					{
						lastseen = client_status[j].lastseen;
					}

					switch(client_status[j].status_error)
					{
					case se_ident_error:
						i_status=11; break;
					case se_too_many_clients:
						i_status=12; break;
					case se_authentication_error:
						i_status=13; break;
					default:
						if(!client_status[j].processes.empty())
						{
							i_status = client_status[j].processes[0].action;
						}
					}

					for(size_t k=0;k<client_status[j].processes.size();++k)
					{
						SProcess& process = client_status[j].processes[k];
						JSON::Object proc;
						proc.set("action", process.action);
						proc.set("pcdone", process.pcdone);
						processes.add(proc);
					}
				}
			}

			stat.set("online", online);
			stat.set("ip", ip);
			stat.set("client_version_string", client_version_string);
			stat.set("os_version_string", os_version_string);
			stat.set("os_simple", os_simple);
			stat.set("status", i_status);
			stat.set("processes", processes);
			stat.set("lastseen", lastseen);

			status.add(stat);
		}

		if(rights=="all")
		{
			bool has_ident_error_clients = false;
			for(size_t i=0;i<client_status.size();++i)
			{
				bool found=false;
				for(size_t j=0;j<res.size();++j)
				{
					if(res[j]["name"]==client_status[i].client)
					{
						found=true;
						break;
					}
				}

				if(found || client_status[i].client.empty()) continue;

				JSON::Object stat;
				stat.set("id", (std::string)"-");
				stat.set("name", client_status[i].client);
				stat.set("lastbackup", (std::string)"-");
				stat.set("lastseen", (std::string)"-");
				stat.set("lastbackup_image", (std::string)"-");
				stat.set("online", client_status[i].r_online);
				stat.set("delete_pending", 0);
				std::string ip;
				unsigned char *ips=(unsigned char*)&client_status[i].ip_addr;
				ip=convert(ips[0])+"."+convert(ips[1])+"."+convert(ips[2])+"."+convert(ips[3]);
				stat.set("ip", ip);

				switch(client_status[i].status_error)
				{
				case se_ident_error:
					stat.set("status", 11); 
					has_ident_error_clients = true;
					break;
				case se_too_many_clients:
					stat.set("status", 12); break;
				case se_authentication_error:
					stat.set("status", 13); break;
				default:
					stat.set("status", 10); break;
				}

				stat.set("file_ok", false);
				stat.set("image_ok", false);
				stat.set("rejected", true);

				status.add(stat);
			}

			if (has_ident_error_clients)
			{
				ret.set("has_ident_error_clients", true);
				ret.set("has_ident_error_clients_stop_show_key", "has_ident_error_clients");
				if (is_stop_show(db, "has_ident_error_clients"))
				{
					ret.set("show_has_ident_error_clients", false);
				}
			}
		}
		JSON::Array extra_clients;

		if(rights=="all")
		{
			res=db->Read("SELECT id, hostname, lastip FROM settings_db.extra_clients");
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object extra_client;

				extra_client.set("hostname", res[i]["hostname"]);

				_i64 i_ip=os_atoi64(res[i]["lastip"]);

				bool online=false;

				for(size_t j=0;j<client_status.size();++j)
				{
					if(i_ip==(_i64)client_status[j].ip_addr)
					{
						online=true;
					}
				}
				extra_client.set("id", res[i]["id"]);
				extra_client.set("online", online);

				extra_clients.add(extra_client);
			}
			ret.set("allow_extra_clients", true);
			ret.set("allow_modify_clients", true);
		}

		ret.set("status", status);
		ret.set("extra_clients", extra_clients);
		ret.set("server_identity", helper.getStrippedServerIdentity());

		if(helper.getRights("remove_client")=="all")
		{
			ret.set("allow_modify_clients", true);
			ret.set("remove_client", true);
		}

		if(helper.getRights("start_backup")=="all")
		{
			ret.set("allow_modify_clients", true);
		}

		if (helper.getRights("add_client") == "all")
		{
			ret.set("allow_add_client", true);
		}

		JSON::Array client_downloads;
		if(client_download(helper, client_downloads))
		{
			ret.set("client_downloads", client_downloads);
		}

		ServerSettings settings(db);
		ret.set("no_images", settings.getSettings()->no_images);
		ret.set("no_file_backups", settings.getSettings()->no_file_backups);

		if(helper.getRights("all")=="all")
		{
			ret.set("admin", JSON::Value(true));
			set_server_version_info(db, ret);
		}

		if(is_big_endian())
		{
			ret.set("big_endian", true);
		}
	}
	else
	{
		ret.set("error", 1);
	}
    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY
