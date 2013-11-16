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
#include "../../urbackupcommon/os_functions.h"
#include "../server_status.h"
#include "../../Interface/Pipe.h"
#include "../server_get.h"
#include "../../cryptoplugin/ICryptoFactory.h"

#include <algorithm>

extern ICryptoFactory *crypto_fak;

namespace
{

bool start_backup(IPipe *comm_pipe, std::wstring backup_type)
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

bool client_download(Helper& helper, JSON::Array &client_downloads)
{
	IDatabase *db=helper.getDatabase();
	ServerSettings settings(db);

	if(!FileExists("urbackup/UrBackupUpdate.exe"))
		return false;

	if(!FileExists("urbackup/UrBackupUpdate.sig"))
		return false;

	if(crypto_fak==NULL)
		return false;

	bool clientid_rights_all;
	std::vector<int> clientid_rights=helper.clientRights(RIGHT_SETTINGS, clientid_rights_all);

	db_results res=db->Read("SELECT id, name FROM clients");

	bool has_client=false;

	for(size_t i=0;i<res.size();++i)
	{
		int clientid=watoi(res[i][L"id"]);
		std::wstring clientname=res[i][L"name"];

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

}

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
			if(!os_directory_exists(os_file_prefix(settings.getSettings()->backupfolder)) || !os_directory_exists(os_file_prefix(settings.getSettings()->backupfolder_uncompr)) || settings.getSettings()->backupfolder.empty())
			{
				ret.set("dir_error", true);
				if(settings.getSettings()->backupfolder.empty())
					ret.set("dir_error_ext", "err_name_empty");
				else if(!os_directory_exists(os_file_prefix(settings.getSettings()->backupfolder)) )
					ret.set("dir_error_ext", "err_folder_not_found");
			}
			else if(!os_directory_exists(os_file_prefix(settings.getSettings()->backupfolder+os_file_sep()+L"clients")) && !os_create_dir(os_file_prefix(settings.getSettings()->backupfolder+os_file_sep()+L"clients")) )
			{
				ret.set("dir_error" ,true);
				ret.set("dir_error_ext", "err_cannot_create_subdir");
			}
			IFile *tmp=Server->openTemporaryFile();
			if(tmp==NULL)
			{
				ret.set("tmpdir_error", true);
			}
			else
			{
				Server->destroy(tmp);
			}

			if(ServerStatus::getServerNospcStalled()>0)
			{
				ret.set("nospc_stalled" ,true);
			}
			if(ServerStatus::getServerNospcFatal())
			{
				ret.set("nospc_fatal" ,true);
			}

			if( (Server->getFailBits() & IServer::FAIL_DATABASE_CORRUPTED) ||
				(Server->getFailBits() & IServer::FAIL_DATABASE_IOERR) )
			{
				ret.set("database_error", true);
			}
		}

		bool details=false;
		if(GET.find(L"details")!=GET.end())
		{
			details=true;
			ret.set("details", true);
		}
		std::wstring hostname=GET[L"hostname"];
		if(!hostname.empty() && rights=="all")
		{
			if(GET[L"remove"]==L"true")
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
		if(GET.find(L"clientname")!=GET.end() && helper.getRights("add_client")=="all" )
		{
			bool new_client=false;
			int id=BackupServerGet::getClientID(db, GET[L"clientname"], NULL, &new_client);
			if(new_client)
			{
				ret.set("added_new_client", true);
			}
		}
		std::wstring s_remove_client=GET[L"remove_client"];
		if(!s_remove_client.empty() && helper.getRights("remove_client")=="all")
		{
			std::vector<std::wstring> remove_client;
			Tokenize(s_remove_client, remove_client, L",");
			if(GET.find(L"stop_remove_client")!=GET.end())
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
					IQuery *q=db->Prepare("UPDATE clients SET delete_pending=1 WHERE id=?");
					q->Bind(remove_client[i]);
					q->Write();
					q->Reset();
				}
			}
		}

		std::wstring s_start_client=GET[L"start_client"];
		std::vector<int> start_client;
		std::wstring start_type=GET[L"start_type"];
		if(!s_start_client.empty() && helper.getRights("start_backup")=="all")
		{
			std::vector<std::wstring> sv_start_client;
			Tokenize(s_start_client, sv_start_client, L",");
			for(size_t i=0;i<sv_start_client.size();++i)
			{
				start_client.push_back(watoi(sv_start_client[i]));
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
		db_results res=db->Read("SELECT id, delete_pending, name, strftime('"+helper.getTimeFormatString()+"', lastbackup, 'localtime') AS lastbackup, strftime('"+helper.getTimeFormatString()+"', lastseen, 'localtime') AS lastseen,"
			"strftime('"+helper.getTimeFormatString()+"', lastbackup_image, 'localtime') AS lastbackup_image FROM clients"+filter);

		double backup_ok_mod_file=3.;
		db_results res_t=db->Read("SELECT value FROM settings_db.settings WHERE key='backup_ok_mod_file' AND clientid=0");
		if(res_t.size()>0)
		{
			backup_ok_mod_file=atof(Server->ConvertToUTF8(res_t[0][L"value"]).c_str());
		}

		double backup_ok_mod_image=3.;
		res_t=db->Read("SELECT value FROM settings_db.settings WHERE key='backup_ok_mod_image' AND clientid=0");
		if(res_t.size()>0)
		{
			backup_ok_mod_image=atof(Server->ConvertToUTF8(res_t[0][L"value"]).c_str());
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
			stat.set("delete_pending", res[i][L"delete_pending"] );

			std::string ip="-";
			std::string client_version_string;
			std::string os_version_string;
			int done_pc=-1;
			int i_status=0;
			bool online=false;
			SStatus *curr_status=NULL;
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
					ip=nconvert(ips[0])+"."+nconvert(ips[1])+"."+nconvert(ips[2])+"."+nconvert(ips[3]);

					client_version_string=client_status[j].client_version_string;
					os_version_string=client_status[j].os_version_string;
					done_pc=client_status[j].pcdone;

					if(client_status[j].wrong_ident)
						i_status=11;
					else if(client_status[j].too_many_clients)
						i_status=12;
					else
						i_status=client_status[j].statusaction;
				}
			}

			if(std::find(start_client.begin(), start_client.end(), clientid)!=start_client.end())
			{
				stat.set("start_type", start_type);
				if(!online || curr_status->comm_pipe==NULL)
				{
					stat.set("start_ok", false);
				}
				else
				{
					if(start_backup(curr_status->comm_pipe, start_type) )
					{
						stat.set("start_ok", true);
					}
					else
					{
						stat.set("start_ok", false);
					}
				}
			}

			stat.set("online", online);
			stat.set("ip", ip);
			stat.set("client_version_string", client_version_string);
			stat.set("os_version_string", os_version_string);
			stat.set("status", i_status);
			stat.set("done_pc", done_pc);
			

			ServerSettings settings(db, clientid);

			int time_filebackup=settings.getUpdateFreqFileIncr();
			if( time_filebackup<0)
			{
				int time_filebackup_full=settings.getUpdateFreqFileFull();
				if(time_filebackup_full<time_filebackup && time_filebackup_full>=0 )
				{
					time_filebackup=time_filebackup_full;
				}
			}
			
			IQuery *q=db->Prepare("SELECT id FROM clients WHERE lastbackup IS NOT NULL AND datetime('now','-"+nconvert((int)(time_filebackup*backup_ok_mod_file+0.5))+" seconds')<lastbackup AND id=?");
			q->Bind(clientid);
			db_results res_file_ok=q->Read();
			q->Reset();
			stat.set("file_ok", !res_file_ok.empty());

			int time_imagebackup=settings.getUpdateFreqImageIncr();
			if( time_imagebackup<0)
			{
				int time_imagebackup_full=settings.getUpdateFreqImageFull();
				if(time_imagebackup_full<time_imagebackup && time_imagebackup_full>=0 )
				{
					time_imagebackup=time_imagebackup_full;
				}
			}

			q=db->Prepare("SELECT id FROM clients WHERE lastbackup_image IS NOT NULL AND datetime('now','-"+nconvert((int)(time_imagebackup*backup_ok_mod_image+0.5))+" seconds')<lastbackup_image AND id=?");
			q->Bind(clientid);
			res_file_ok=q->Read();
			q->Reset();
			stat.set("image_ok", !res_file_ok.empty());

			status.add(stat);
		}

		if(rights=="all")
		{
			for(size_t i=0;i<client_status.size();++i)
			{
				bool found=false;
				for(size_t j=0;j<res.size();++j)
				{
					if(res[j][L"name"]==client_status[i].client)
					{
						found=true;
						break;
					}
				}

				if(found) continue;

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
				ip=nconvert(ips[0])+"."+nconvert(ips[1])+"."+nconvert(ips[2])+"."+nconvert(ips[3]);
				stat.set("ip", ip);

				if(client_status[i].wrong_ident)
					stat.set("status", 11);
				else if(client_status[i].too_many_clients)
					stat.set("status", 12);
				else
					stat.set("status", 10);

				stat.set("file_ok", false);
				stat.set("image_ok", false);
				stat.set("rejected", true);

				status.add(stat);
			}
		}
		JSON::Array extra_clients;

		if(rights=="all")
		{
			res=db->Read("SELECT id, hostname, lastip FROM settings_db.extra_clients");
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
			ret.set("allow_extra_clients", true);
			ret.set("allow_modify_clients", true);
		}

		ret.set("status", status);
		ret.set("extra_clients", extra_clients);
		ret.set("server_identity", helper.getStrippedServerIdentity());

		if(helper.getRights("remove_client")=="all")
		{
			ret.set("remove_client", true);
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
		}
	}
	else
	{
		ret.set("error", 1);
	}
	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY