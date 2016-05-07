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

void set_server_version_info(JSON::Object& ret)
{
	std::auto_ptr<ISettingsReader> infoProperties(Server->createFileSettingsReader("urbackup/server_version_info.properties"));

	if(infoProperties.get())
	{
		std::string curr_version_num;
		if(infoProperties->getValue("curr_version_num", &curr_version_num))
		{
			ret.set("curr_version_num", watoi64(curr_version_num));
		}

		std::string curr_version_str;
		if(infoProperties->getValue("curr_version_str", &curr_version_str))
		{
			ret.set("curr_version_str", curr_version_str);
		}
	}
}

void access_dir_details(std::string folder, std::string& ret)
{
	bool has_error = false;
	getFiles(folder, &has_error);
	if (has_error)
	{
		ret += "Cannot access " + folder + ". " + os_last_error_str() + "\n";
	}
	else
	{
		ret += "Can access " + folder + "\n";
	}
}

std::string access_err_details(std::string folder)
{
	std::vector<std::string> toks;
	TokenizeMail(folder, toks, os_file_sep());

	std::string ret;

	std::string cdir = os_file_sep();
	access_dir_details(cdir, ret);

	for (size_t i = 0; i < toks.size(); ++i)
	{
		if (toks[i].empty()) continue;

		if (cdir!=os_file_sep())
		{
			cdir += os_file_sep();
		}
		cdir += toks[i];

		access_dir_details(cdir, ret);
	}

	return ret;
}

std::string access_dir_hint(std::string folder)
{
	if (folder.size() > 1 && folder[1] == ':')
	{
		bool has_error = false;
		getFiles(folder.substr(0, 2) + os_file_sep(), &has_error);

		if (has_error)
		{
			return "volume_not_accessible";
		}
	}

	if ( (folder.size() > 2 && folder[0] == '\\'
		&& folder[1] == '\\'
		&& folder[2] != '?' )
		|| next(folder, 0, "\\\\?\\UNC") )
	{
		bool has_error = false;
		getFiles(folder, &has_error);

		if (has_error && os_last_error() == 5
			|| os_last_error()== 1326 )
		{
			return "folder_unc_access_denied";
		}
	}

	return std::string();
}

void access_dir_checks(ServerSettings& settings, std::string backupfolder, std::string backupfolder_uncompr,
	JSON::Object& ret)
{
#ifdef _WIN32
	if (backupfolder.size() == 2 && backupfolder[1] == ':')
	{
		backupfolder += os_file_sep();
	}
	if (backupfolder_uncompr.size() == 2 && backupfolder_uncompr[1] == ':')
	{
		backupfolder_uncompr += os_file_sep();
	}
#endif

	if (backupfolder.empty() || !os_directory_exists(os_file_prefix(backupfolder)) || !os_directory_exists(os_file_prefix(backupfolder_uncompr)))
	{
		if (!backupfolder.empty())
		{
			ret.set("system_err", os_last_error_str());
		}

		ret.set("dir_error", true);
		if (settings.getSettings()->backupfolder.empty())
			ret.set("dir_error_ext", "err_name_empty");
		else if (!os_directory_exists(os_file_prefix(settings.getSettings()->backupfolder)))
			ret.set("dir_error_ext", "err_folder_not_found");

#ifdef _WIN32
		std::string hint = access_dir_hint(settings.getSettings()->backupfolder);
		if (!hint.empty())
		{
			ret.set("dir_error_hint", hint);
		}
#endif

#ifndef _WIN32
		ret.set("detail_err_str", access_err_details(settings.getSettings()->backupfolder));
#endif
	}
	else if (!os_directory_exists(os_file_prefix(backupfolder + os_file_sep() + "clients")) && !os_create_dir(os_file_prefix(backupfolder + os_file_sep() + "clients")))
	{
		ret.set("system_err", os_last_error_str());
		ret.set("dir_error", true);
		ret.set("dir_error_ext", "err_cannot_create_subdir");

#ifdef _WIN32
		std::string hint = access_dir_hint(backupfolder);
		if (!hint.empty())
		{
			ret.set("dir_error_hint", hint);
		}
#endif
	}
	else
	{
		bool has_access_error = false;
		std::string testfoldername = "testfolderHvfgh---dFFoeRRRf";
		std::string testfolderpath = backupfolder + os_file_sep() + testfoldername;
		if (os_directory_exists(os_file_prefix(testfolderpath)))
		{
			if (!os_remove_dir(os_file_prefix(testfolderpath)))
			{
				ret.set("system_err", os_last_error_str());
				ret.set("dir_error", true);
				ret.set("dir_error_ext", "err_cannot_create_subdir");
				has_access_error = true;
#ifdef _WIN32
				std::string hint = access_dir_hint(backupfolder);
				if (!hint.empty())
				{
					ret.set("dir_error_hint", hint);
				}
#endif
			}
		}

		if (!has_access_error
			&& !os_create_dir(os_file_prefix(testfolderpath)))
		{
			ret.set("system_err", os_last_error_str());
			ret.set("dir_error", true);
			ret.set("dir_error_ext", "err_cannot_create_subdir");

#ifdef _WIN32
			std::string hint = access_dir_hint(backupfolder);
			if (!hint.empty())
			{
				ret.set("dir_error_hint", hint);
			}
#endif
			has_access_error = true;
		}

		std::string linkfolderpath = testfolderpath + "_link";
		os_remove_symlink_dir(os_file_prefix(linkfolderpath));

		if (!has_access_error
			&& !os_link_symbolic(os_file_prefix(testfolderpath), os_file_prefix(linkfolderpath)) )
		{
			ret.set("system_err", os_last_error_str());
			ret.set("dir_error", true);
			ret.set("dir_error_ext", "err_cannot_create_symbolic_links");
			ret.set("dir_error_hint", "UrBackup cannot create symbolic links on the backup storage. "
				"Your backup storage must support symbolic links in order for UrBackup to work correctly. "
				"The UrBackup Server must run as administrative user on Windows. "
				"Note: As of 2016-05-07 samba has not implemented the necessary functionality to support symbolic link creation from Windows.");
		}

		os_remove_symlink_dir(os_file_prefix(linkfolderpath));

		if (!has_access_error
			&& !os_remove_dir(os_file_prefix(testfolderpath)))
		{
			ret.set("system_err", os_last_error_str());
			ret.set("dir_error", true);
			ret.set("dir_error_ext", "err_cannot_create_subdir");
			has_access_error = true;
		}
	}

	IFile *tmp = Server->openTemporaryFile();
	ScopedDeleteFile delete_tmp_file(tmp);
	if (tmp == NULL)
	{
		ret.set("tmpdir_error", true);
	}
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
		{
			ServerSettings settings(db);

			access_dir_checks(settings, settings.getSettings()->backupfolder,
				settings.getSettings()->backupfolder_uncompr, ret);

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
				filter+="id="+convert(clientids[i]);
				if(i+1<clientids.size())
					filter+=" OR ";
			}
		}
		db_results res=db->Read("SELECT id, delete_pending, name, strftime('"+helper.getTimeFormatString()+"', lastbackup) AS lastbackup, strftime('"+helper.getTimeFormatString()+"', lastseen) AS lastseen,"
			"strftime('"+helper.getTimeFormatString()+"', lastbackup_image) AS lastbackup_image FROM clients"+filter+" ORDER BY name");

		double backup_ok_mod_file=3.;
		db_results res_t=db->Read("SELECT value FROM settings_db.settings WHERE key='backup_ok_mod_file' AND clientid=0");
		if(res_t.size()>0)
		{
			backup_ok_mod_file=atof((res_t[0]["value"]).c_str());
		}

		double backup_ok_mod_image=3.;
		res_t=db->Read("SELECT value FROM settings_db.settings WHERE key='backup_ok_mod_image' AND clientid=0");
		if(res_t.size()>0)
		{
			backup_ok_mod_image=atof((res_t[0]["value"]).c_str());
		}

		std::vector<SStatus> client_status=ServerStatus::getStatus();

		for(size_t i=0;i<res.size();++i)
		{
			JSON::Object stat;
			int clientid=watoi(res[i]["id"]);
			std::string clientname=res[i]["name"];
			stat.set("id", clientid);
			stat.set("name", clientname);
			stat.set("lastbackup", watoi64(res[i]["lastbackup"]));
			stat.set("lastseen", watoi64(res[i]["lastseen"]));
			stat.set("lastbackup_image", watoi64(res[i]["lastbackup_image"]));
			stat.set("delete_pending", res[i]["delete_pending"] );

			std::string ip="-";
			std::string client_version_string;
			std::string os_version_string;
			int i_status=0;
			bool online=false;
			SStatus *curr_status=NULL;
			JSON::Array processes;

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
			stat.set("status", i_status);
			stat.set("processes", processes);

			ServerSettings settings(db, clientid);

			int time_filebackup=settings.getUpdateFreqFileIncr();
			int time_filebackup_full=settings.getUpdateFreqFileFull();
			if( time_filebackup_full>=0 &&
				(time_filebackup<0 || time_filebackup_full<time_filebackup) )
			{
				time_filebackup=time_filebackup_full;
			}
			
			IQuery *q=db->Prepare("SELECT id FROM clients WHERE lastbackup IS NOT NULL AND datetime('now','-"+convert((int)(time_filebackup*backup_ok_mod_file+0.5))+" seconds')<lastbackup AND id=?");
			q->Bind(clientid);
			db_results res_file_ok=q->Read();
			q->Reset();
			stat.set("file_ok", !res_file_ok.empty());

			int time_imagebackup=settings.getUpdateFreqImageIncr();
			int time_imagebackup_full=settings.getUpdateFreqImageFull();
			if( time_imagebackup_full>=0 &&
				(time_imagebackup<0 || time_imagebackup_full<time_imagebackup) )
			{
				time_imagebackup=time_imagebackup_full;
			}

			q=db->Prepare("SELECT id FROM clients WHERE lastbackup_image IS NOT NULL AND datetime('now','-"+convert((int)(time_imagebackup*backup_ok_mod_image+0.5))+" seconds')<lastbackup_image AND id=?");
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
					if(res[j]["name"]==client_status[i].client)
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
				ip=convert(ips[0])+"."+convert(ips[1])+"."+convert(ips[2])+"."+convert(ips[3]);
				stat.set("ip", ip);

				switch(client_status[i].status_error)
				{
				case se_ident_error:
					stat.set("status", 11); break;
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
			set_server_version_info(ret);
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
