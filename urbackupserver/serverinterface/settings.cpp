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
#include "../../Interface/SettingsReader.h"
#include "../../urlplugin/IUrlFactory.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../server_get.h"
#include "../server_archive.h"

extern IUrlFactory *url_fak;
extern ICryptoFactory *crypto_fak;

std::vector<std::wstring> getMailSettingsList(void)
{
	std::vector<std::wstring> tmp;
	tmp.push_back(L"mail_servername");
	tmp.push_back(L"mail_serverport");
	tmp.push_back(L"mail_username");
	tmp.push_back(L"mail_password");
	tmp.push_back(L"mail_from");
	tmp.push_back(L"mail_ssl_only");
	tmp.push_back(L"mail_check_certificate");
	return tmp;
}

JSON::Object getJSONClientSettings(ServerSettings &settings)
{
	JSON::Object ret;
	ret.set("update_freq_incr", settings.getSettings()->update_freq_incr);
	ret.set("update_freq_full", settings.getSettings()->update_freq_full);
	ret.set("update_freq_image_full", settings.getSettings()->update_freq_image_full);
	ret.set("update_freq_image_incr", settings.getSettings()->update_freq_image_incr);
	ret.set("max_file_incr", settings.getSettings()->max_file_incr);
	ret.set("min_file_incr", settings.getSettings()->min_file_incr);
	ret.set("max_file_full", settings.getSettings()->max_file_full);
	ret.set("min_file_full", settings.getSettings()->min_file_full);
	ret.set("min_image_incr", settings.getSettings()->min_image_incr);
	ret.set("max_image_incr", settings.getSettings()->max_image_incr);
	ret.set("min_image_full", settings.getSettings()->min_image_full);
	ret.set("max_image_full", settings.getSettings()->max_image_full);
	ret.set("allow_overwrite", settings.getSettings()->allow_overwrite);
	ret.set("startup_backup_delay", settings.getSettings()->startup_backup_delay);
	ret.set("backup_window", settings.getSettings()->backup_window);
	ret.set("computername", settings.getSettings()->computername);
	ret.set("exclude_files", settings.getSettings()->exclude_files);
	ret.set("include_files", settings.getSettings()->include_files);
	ret.set("default_dirs", settings.getSettings()->default_dirs);
	ret.set("allow_config_paths", settings.getSettings()->allow_config_paths);
	ret.set("allow_starting_file_backups", settings.getSettings()->allow_starting_file_backups);
	ret.set("allow_starting_image_backups", settings.getSettings()->allow_starting_image_backups);
	ret.set("allow_pause", settings.getSettings()->allow_pause);
	ret.set("allow_log_view", settings.getSettings()->allow_log_view);
	ret.set("image_letters", settings.getSettings()->image_letters);
	ret.set("internet_authkey", settings.getSettings()->internet_authkey);
	ret.set("client_set_settings", settings.getSettings()->client_set_settings);
	ret.set("internet_speed", settings.getSettings()->internet_speed);
	ret.set("local_speed", settings.getSettings()->local_speed);
	ret.set("internet_mode_enabled", settings.getSettings()->internet_mode_enabled);
	ret.set("internet_compress", settings.getSettings()->internet_compress);
	ret.set("internet_encrypt", settings.getSettings()->internet_encrypt);
	ret.set("internet_image_backups", settings.getSettings()->internet_image_backups);
	ret.set("internet_full_file_backups", settings.getSettings()->internet_full_file_backups);
	ret.set("silent_update", settings.getSettings()->silent_update);
	return ret;
}

struct SGeneralSettings
{
	SGeneralSettings(void): no_images(false), no_file_backups(false), autoshutdown(false), autoupdate_clients(true),
		max_sim_backups(10), max_active_clients(100), cleanup_window(L"1-7/3-4"), backup_database(true),
		internet_server_port(55415),
		global_local_speed(-1), global_internet_speed(-1) {}
	std::wstring backupfolder;
	bool no_images;
	bool no_file_backups;
	bool autoshutdown;
	bool autoupdate_clients;
	int max_sim_backups;
	int max_active_clients;
	std::wstring tmpdir;
	std::wstring cleanup_window;
	bool backup_database;
	std::string internet_server;
	unsigned short internet_server_port;
	int global_local_speed;
	int global_internet_speed;
};

struct SClientSettings
{
	SClientSettings(void) : overwrite(false) {}
	bool overwrite;
};

SGeneralSettings getGeneralSettings(IDatabase *db)
{
	IQuery *q=db->Prepare("SELECT key, value FROM settings_db.settings WHERE clientid=0");
	db_results res=q->Read();
	q->Reset();
	SGeneralSettings ret;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring key=res[i][L"key"];
		std::wstring value=res[i][L"value"];
		if(key==L"backupfolder")
			ret.backupfolder=value;
		else if(key==L"no_images" && value==L"true")
			ret.no_images=true;
		else if(key==L"no_file_backups" && value==L"true")
			ret.no_file_backups=true;
		else if(key==L"autoshutdown" && value==L"true")
			ret.autoshutdown=true;
		else if(key==L"autoupdate_clients" && value==L"false")
			ret.autoupdate_clients=false;
		else if(key==L"max_active_clients")
			ret.max_active_clients=watoi(value);
		else if(key==L"max_sim_backups")
			ret.max_sim_backups=watoi(value);
		else if(key==L"tmpdir")
			ret.tmpdir=value;
		else if(key==L"cleanup_window")
			ret.cleanup_window=value;
		else if(key==L"backup_database" && value==L"false")
			ret.backup_database=false;
		else if(key==L"internet_server" )
			ret.internet_server=Server->ConvertToUTF8(value);
		else if(key==L"internet_server_port" )
			ret.internet_server_port=(unsigned short)watoi(value);
		else if(key==L"global_internet_speed")
			ret.global_internet_speed=watoi(value);
		else if(key==L"global_local_speed")
			ret.global_local_speed=watoi(value);
	}
	return ret;
}

void getMailSettings(JSON::Object &obj, IDatabase *db)
{
	std::vector<std::wstring> slist=getMailSettingsList();
	IQuery *q=db->Prepare("SELECT key, value FROM settings_db.settings WHERE clientid=0 AND key=?");
	for(size_t i=0;i<slist.size();++i)
	{
		q->Bind(slist[i]);
		db_results res=q->Read();
		q->Reset();
		if(!res.empty())
		{
			obj.set(Server->ConvertToUTF8(slist[i]), res[0][L"value"]);
		}
		else
		{
			std::string v="";
			if(slist[i]==L"mail_serverport")
				v="25";
			obj.set(Server->ConvertToUTF8(slist[i]), v);
		}
	}
}

SClientSettings getClientSettings(IDatabase *db, int clientid)
{
	IQuery *q=db->Prepare("SELECT key, value FROM settings_db.settings WHERE clientid=?");
	q->Bind(clientid);
	db_results res=q->Read();
	q->Reset();
	SClientSettings ret;
	for(size_t i=0;i<res.size();++i)
	{
		std::wstring key=res[i][L"key"];
		std::wstring value=res[i][L"value"];
		if(key==L"overwrite" && value==L"true")
			ret.overwrite=true;
	}
	return ret;
}

void updateSetting(const std::wstring &key, const std::wstring &value, IQuery *q_get, IQuery *q_update, IQuery *q_insert)
{
	q_get->Bind(key);
	db_results r_get=q_get->Read();
	q_get->Reset();
	if(r_get.empty())
	{
		q_insert->Bind(key);
		q_insert->Bind(value);
		q_insert->Write();
		q_insert->Reset();
	}
	else if( r_get[0][L"value"]!=value )
	{
		q_update->Bind(value);
		q_update->Bind(key);
		q_update->Write();
		q_update->Reset();
	}
}

void saveGeneralSettings(SGeneralSettings settings, IDatabase *db)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,0)");

	updateSetting(L"backupfolder", settings.backupfolder, q_get, q_update, q_insert);
	updateSetting(L"no_images", settings.no_images?L"true":L"false", q_get, q_update, q_insert);
	updateSetting(L"no_file_backups", settings.no_file_backups?L"true":L"false", q_get, q_update, q_insert);
	updateSetting(L"autoshutdown", settings.autoshutdown?L"true":L"false",  q_get, q_update, q_insert);
	updateSetting(L"autoupdate_clients", settings.autoupdate_clients?L"true":L"false",  q_get, q_update, q_insert);
	updateSetting(L"max_sim_backups", convert(settings.max_sim_backups),  q_get, q_update, q_insert);
	updateSetting(L"max_active_clients", convert(settings.max_active_clients),  q_get, q_update, q_insert);
	updateSetting(L"tmpdir", settings.tmpdir,  q_get, q_update, q_insert);
	updateSetting(L"cleanup_window", settings.cleanup_window,  q_get, q_update, q_insert);
	updateSetting(L"backup_database", settings.backup_database?L"true":L"false",  q_get, q_update, q_insert);
	updateSetting(L"global_local_speed", convert(settings.global_local_speed),  q_get, q_update, q_insert);
	updateSetting(L"global_internet_speed", convert(settings.global_internet_speed),  q_get, q_update, q_insert);

#ifdef _WIN32
	if(!settings.tmpdir.empty())
	{
		os_create_dir(settings.tmpdir+os_file_sep()+L"urbackup_tmp");
		Server->setTemporaryDirectory(settings.tmpdir+os_file_sep()+L"urbackup_tmp");
	}
#endif
}

void updateMailSettings(str_map &GET, IDatabase *db)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,0)");

	std::vector<std::wstring> settings=getMailSettingsList();
	for(size_t i=0;i<settings.size();++i)
	{
		str_map::iterator it=GET.find(settings[i]);
		if(it!=GET.end())
		{
			updateSetting(settings[i], it->second, q_get, q_update, q_insert);
		}
	}
}

void updateInternetSettings(SGeneralSettings settings, IDatabase *db)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,0)");

	updateSetting(L"internet_server", Server->ConvertToUnicode(settings.internet_server), q_get, q_update, q_insert);
	updateSetting(L"internet_server_port", convert(settings.internet_server_port), q_get, q_update, q_insert);
}

void saveClientSettings(SClientSettings settings, IDatabase *db, int clientid)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid="+nconvert(clientid)+" AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid="+nconvert(clientid));
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,"+nconvert(clientid)+")");

	updateSetting(L"overwrite", settings.overwrite?L"true":L"false", q_get, q_update, q_insert);
}

void updateClientSettings(int t_clientid, str_map &GET, IDatabase *db)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE key=? AND clientid=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=?");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,?)");

	std::vector<std::wstring> sset=getSettingsList();
	sset.push_back(L"allow_overwrite");
	for(size_t i=0;i<sset.size();++i)
	{
		str_map::iterator it=GET.find(sset[i]);
		if(it!=GET.end())
		{
			q_get->Bind(sset[i]);
			q_get->Bind(t_clientid);
			db_results res=q_get->Read();
			q_get->Reset();
			if(!res.empty())
			{
				q_update->Bind(it->second);
				q_update->Bind(sset[i]);
				q_update->Bind(t_clientid);
				q_update->Write();
				q_update->Reset();
			}
			else
			{
				q_insert->Bind(sset[i]);
				q_insert->Bind(it->second);
				q_insert->Bind(t_clientid);
				q_insert->Write();
				q_insert->Reset();
			}
		}
	}
}

void updateRights(int t_userid, std::string s_rights, IDatabase *db)
{
	str_map rights;
	ParseParamStr(s_rights, &rights);
	
	IQuery *q_del=db->Prepare("DELETE FROM settings_db.si_permissions WHERE clientid=?");
	q_del->Bind(t_userid);
	q_del->Write();
	q_del->Reset();

	str_map::iterator idx=rights.find(L"idx");

	if(idx!=rights.end())
	{
		std::vector<std::wstring> elms;
		Tokenize(idx->second, elms, L",");

		if(!elms.empty())
		{
			IQuery *q_insert=db->Prepare("INSERT INTO settings_db.si_permissions (t_domain, t_right, clientid) VALUES (?,?,?)");

			for(size_t i=0;i<elms.size();++i)
			{
				str_map::iterator it_domain=rights.find(elms[i]+L"_domain");
				str_map::iterator it_right=rights.find(elms[i]+L"_right");
				if(it_domain!=rights.end() && it_right!=rights.end())
				{
					q_insert->Bind(it_domain->second);
					q_insert->Bind(it_right->second);
					q_insert->Bind(t_userid);
					q_insert->Write();
					q_insert->Reset();
				}
			}
		}
	}
}

void updateArchiveSettings(int clientid, str_map &GET, IDatabase *db)
{
	int i=0;
	IQuery *q=db->Prepare("DELETE FROM settings_db.automatic_archival WHERE clientid=?");
	q->Bind(clientid);
	q->Write();
	q=db->Prepare("INSERT INTO settings_db.automatic_archival (next_archival, interval, interval_unit, length, length_unit, backup_types, clientid, archive_window) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
	while(GET.find(L"archive_every_"+convert(i))!=GET.end())
	{
		_i64 archive_next=watoi64(GET[L"archive_next_"+convert(i)]);
		int archive_every=watoi(GET[L"archive_every_"+convert(i)]);
		int archive_for=watoi(GET[L"archive_for_"+convert(i)]);
		std::wstring backup_type_str=GET[L"archive_backup_type_"+convert(i)];
		int backup_types=ServerAutomaticArchive::getBackupTypes(backup_type_str);


		if(archive_next<0)
		{
			if(clientid==0)
			{
				q->Bind(0);
			}
			else
			{
				q->Bind(Server->getTimeSeconds());
			}
		}
		else
		{
			q->Bind(archive_next);
		}
		q->Bind(archive_every);
		q->Bind(GET[L"archive_every_unit_"+convert(i)]);
		q->Bind(archive_for);
		q->Bind(GET[L"archive_for_unit_"+convert(i)]);
		q->Bind(backup_types);
		q->Bind(clientid);
		q->Bind(GET[L"archive_window_"+convert(i)]);
		q->Write();
		q->Reset();

		++i;
	}

	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid="+nconvert(clientid)+" AND key=?");
	if(clientid!=0)
	{		
		IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid="+nconvert(clientid));
		IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,"+nconvert(clientid)+")");

		updateSetting(L"overwrite_archive_settings", L"true", q_get, q_update, q_insert);
	}
	else
	{
		db->Write("DELETE FROM settings_db.settings WHERE key='archive_settings_copied'");
	}
}

void getArchiveSettings(JSON::Object &obj, IDatabase *db, int clientid)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid="+nconvert(clientid)+" AND key=?");
	q_get->Bind("overwrite");
	db_results res=q_get->Read();
	q_get->Reset();
	if(res.empty() || res[0][L"value"]!=L"true")
		clientid=0;

	q_get->Bind("overwrite_archive_settings");
	res=q_get->Read();
	if(res.empty() || res[0][L"value"]!=L"true")
		clientid=0;

	IQuery *q=db->Prepare("SELECT next_archival, interval, interval_unit, length, length_unit, backup_types FROM settings_db.automatic_archival WHERE clientid=?");
	q->Bind(clientid);
	res=q->Read();

	JSON::Array arr;
	for(size_t i=0;i<res.size();++i)
	{
		_i64 archive_next=watoi64(res[i][L"next_archival"]);

		JSON::Object ca;
		ca.set("next_archival", res[i][L"next_archival"]);
		ca.set("archive_every", watoi(res[i][L"interval"]));
		ca.set("archive_every_unit", res[i][L"interval_unit"]);
		ca.set("archive_for", watoi(res[i][L"length"]));
		ca.set("archive_for_unit", res[i][L"length_unit"]);
		ca.set("archive_backup_type", ServerAutomaticArchive::getBackupType(watoi(res[i][L"backup_types"])));
		ca.set("archive_window", res[i][L"archive_window"]);

		if(archive_next>0 && clientid!=0)
		{
			_i64 tl=archive_next-(_i64)Server->getTimeSeconds();
			ca.set("archive_timeleft", tl);
		}
		else
		{
			ca.set("archive_timeleft", "-");
		}

		arr.add(ca);
	}
	obj.set("archive_settings", arr);
}

ACTION_IMPL(settings)
{
	Helper helper(tid, &GET, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==-1) return;
	std::wstring sa=GET[L"sa"];
	int t_clientid=watoi(GET[L"t_clientid"]);
	std::string rights=helper.getRights("settings");
	std::vector<int> clientid;
	IDatabase *db=helper.getDatabase();
	if(rights!="all" && rights!="none" )
	{
		std::vector<std::string> s_clientid;
		Tokenize(rights, s_clientid, ",");
		for(size_t i=0;i<s_clientid.size();++i)
		{
			clientid.push_back(atoi(s_clientid[i].c_str()));
		}
	}
	if(sa.empty())
	{
		if(rights=="all" || helper.getRights("general_settings")=="all")
		{
			sa=L"general";
		}
		else if(helper.getRights("usermod")=="all")
		{
			sa=L"listusers";
		}
		else if(!clientid.empty() )
		{
			sa=L"clientsettings";
			t_clientid=clientid[0];
		}
	}

	if(session!=NULL && rights!="none")
	{
		//navitems
		{
			JSON::Object navitems;

			if(helper.getRights("usermod")=="all" )
			{
				navitems.set("users", true);
			}

			if(helper.getRights("mail_settings")=="all" && url_fak!=NULL )
			{
				navitems.set("mail", true);
			}
			if(crypto_fak!=NULL)
			{
				navitems.set("internet", true);
			}

			JSON::Array clients;
			
			if(rights=="all" )
			{
				IQuery *q=db->Prepare("SELECT id,name FROM clients");
				db_results res=q->Read();
				q->Reset();
				for(size_t i=0;i<res.size();++i)
				{
					JSON::Object u;
					u.set("id",watoi(res[i][L"id"]));
					u.set("name", res[i][L"name"]);
					clients.add(u);
				}
			}
			else
			{
				IQuery *q=db->Prepare("SELECT name FROM clients WHERE id=?");
				for(size_t i=0;i<clientid.size();++i)
				{
					q->Bind(clientid[i]);
					db_results res=q->Read();
					q->Reset();
					JSON::Object u;
					u.set("id", clientid[i]);
					u.set("name", res[0][L"name"]);
					clients.add(u);
				}
			}
			navitems.set("clients", clients);

			if(helper.getRights("general_settings")=="all" )
			{
				navitems.set("general", true);
			}

			ret.set("navitems", navitems);
		}

		if(sa==L"clientsettings" || sa==L"clientsettings_save")
		{
			bool r_ok=false;
			if(rights=="all")
			{
				r_ok=true;
			}
			else
			{
				for(size_t i=0;i<clientid.size();++i)
				{
					if(clientid[i]==t_clientid)
					{
						r_ok=true;
						break;
					}
				}
			}
			if(r_ok)
			{
				SClientSettings s;
				
				if(sa==L"clientsettings_save")
				{
					s.overwrite=(GET[L"overwrite"]==L"true");
					if(s.overwrite)
					{
						updateClientSettings(t_clientid, GET, db);
						updateArchiveSettings(t_clientid, GET, db);
					}
					
					saveClientSettings(s, db, t_clientid);
					if(GET[L"no_ok"]!=L"true")
					{
						ret.set("saved_ok", true);
					}
					else
					{
						ret.set("saved_part", true);
					}

					ServerSettings::updateAll();
				}
				
				ServerSettings settings(db, t_clientid);
				
				JSON::Object obj=getJSONClientSettings(settings);
				s=getClientSettings(db, t_clientid);
				obj.set("overwrite", s.overwrite);
				obj.set("clientid", t_clientid);
				ret.set("settings",  obj);

				getArchiveSettings(ret, db, t_clientid);
				
				sa=L"clientsettings";
				ret.set("sa", sa);
			}
		}
		else if(sa==L"useradd" && helper.getRights("usermod")=="all")
		{
			std::wstring name=strlower(GET[L"name"]);
			std::wstring salt=GET[L"salt"];
			std::wstring pwmd5=GET[L"pwmd5"];

			IQuery *q_find=db->Prepare("SELECT id FROM settings_db.si_users WHERE name=?");
			q_find->Bind(name);
			db_results res=q_find->Read();
			q_find->Reset();


			if(res.empty())
			{
				IQuery *q=db->Prepare("INSERT INTO settings_db.si_users (name, password_md5, salt) VALUES (?,?,?)");
				q->Bind(name);
				q->Bind(pwmd5);
				q->Bind(salt);
				q->Write();
				q->Reset();

				ret.set("add_ok", true);

				int t_userid=(int)db->getLastInsertID();

				std::string s_rights=Server->ConvertToUTF8(GET[L"rights"]);
				updateRights(t_userid, s_rights, db);
			}
			else
			{
				ret.set("alread_exists", true);
			}			

			sa=L"listusers";
			
		}
		if(sa==L"changepw" && ( helper.getRights("usermod")=="all" || GET[L"userid"]==L"own" ) )
		{
			bool ok=true;
			if(GET[L"userid"]==L"own")
			{
				if(!helper.checkPassword(session->mStr[L"username"], GET[L"old_pw"], NULL) )
				{
					ok=false;
				}
			}
			if(ok)
			{
				std::wstring salt=GET[L"salt"];
				std::wstring pwmd5=GET[L"pwmd5"];
				int t_userid;
				if(GET[L"userid"]==L"own")
				{
					t_userid=session->id;
				}
				else
				{
					t_userid=watoi(GET[L"userid"]);
				}
				IQuery *q=db->Prepare("UPDATE settings_db.si_users SET salt=?, password_md5=? WHERE id=?");
			
				q->Bind(salt);
				q->Bind(pwmd5);
				q->Bind(t_userid);
				q->Write();
				q->Reset();
				ret.set("change_ok", true);

				if(GET[L"userid"]!=L"own")
				{
					sa=L"listusers";
				}
			}
			else
			{
				ret.set("old_pw_wrong", true);
			}
		}
		if(sa==L"updaterights" && helper.getRights("usermod")=="all")
		{
			int t_userid=watoi(GET[L"userid"]);
			std::string s_rights=Server->ConvertToUTF8(GET[L"rights"]);
			updateRights(t_userid, s_rights, db);
			sa=L"listusers";
			ret.set("update_right", true);
		}
		if(sa==L"removeuser" && helper.getRights("usermod")=="all")
		{
			int userid=watoi(GET[L"userid"]);

			IQuery *q=db->Prepare("DELETE FROM settings_db.si_users WHERE id=?");
			q->Bind(userid);
			q->Write();
			q->Reset();
			ret.set("removeuser", true);
			sa=L"listusers";
		}
		if(sa==L"listusers" && helper.getRights("usermod")=="all" )
		{
			IQuery *q=db->Prepare("SELECT id,name FROM settings_db.si_users");
			db_results res=q->Read();
			q->Reset();
			q=db->Prepare("SELECT t_right, t_domain FROM settings_db.si_permissions WHERE clientid=?");
			JSON::Array users;
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object obj;
				obj.set("name", res[i][L"name"]);
				obj.set("id", res[i][L"id"]);
				
				q->Bind(res[i][L"id"]);
				db_results res_r=q->Read();
				q->Reset();

				JSON::Array rights;
				for(size_t j=0;j<res_r.size();++j)
				{
					JSON::Object o;
					o.set("right", res_r[j][L"t_right"]);
					o.set("domain", res_r[j][L"t_domain"]);
					rights.add(o);
				}

				obj.set("rights", rights);

				users.add(obj);
			}
			ret.set("users", users);
			ret.set("sa", sa);
		}
		
		if(helper.getRights("general_settings")=="all")
		{
			if(sa==L"general_save")
			{
				SGeneralSettings settings;
				settings.backupfolder=GET[L"backupfolder"];
				settings.no_images=(GET[L"no_images"]==L"true");
				settings.no_file_backups=(GET[L"no_file_backups"]==L"true");
				settings.autoshutdown=(GET[L"autoshutdown"]==L"true");
				settings.autoupdate_clients=(GET[L"autoupdate_clients"]==L"true");
				settings.backup_database=(GET[L"backup_database"]==L"true");
				settings.max_active_clients=watoi(GET[L"max_active_clients"]);
				settings.max_sim_backups=watoi(GET[L"max_sim_backups"]);
				settings.tmpdir=GET[L"tmpdir"];
				settings.cleanup_window=GET[L"cleanup_window"];
				settings.global_internet_speed=watoi(GET[L"global_internet_speed"]);
				settings.global_local_speed=watoi(GET[L"global_local_speed"]);
				settings.internet_server=Server->ConvertToUTF8(GET[L"internet_server"] );
				settings.internet_server_port=watoi(GET[L"internet_server_port"]);

				updateClientSettings(0, GET, db);
				updateArchiveSettings(0, GET, db);
				updateInternetSettings(settings, db);
				saveGeneralSettings(settings, db);

				ServerSettings::updateAll();

				ret.set("saved_ok", true);
				sa=L"general";
			}
			if((sa==L"navitems" || sa==L"general") )
			{
				sa=L"general";
				ret.set("sa", sa);

				SGeneralSettings settings=getGeneralSettings(db);
				ServerSettings serv_settings(db);
				JSON::Object obj=getJSONClientSettings(serv_settings);
				obj.set("backupfolder", settings.backupfolder);
				obj.set("no_images", settings.no_images);
				obj.set("no_file_backups", settings.no_file_backups);
				obj.set("autoshutdown", settings.autoshutdown);
				obj.set("autoupdate_clients", settings.autoupdate_clients);
				obj.set("max_sim_backups", settings.max_sim_backups);
				obj.set("max_active_clients", settings.max_active_clients);
				obj.set("tmpdir", settings.tmpdir);
				obj.set("cleanup_window", settings.cleanup_window);
				obj.set("backup_database", settings.backup_database);
				obj.set("global_local_speed", settings.global_local_speed);
				obj.set("global_internet_speed", settings.global_internet_speed);
				obj.set("internet_server", settings.internet_server);
				obj.set("internet_server_port", settings.internet_server_port);
				#ifdef _WIN32
				obj.set("ONLY_WIN32_BEGIN","");
				obj.set("ONLY_WIN32_END","");
				#else
				obj.set("ONLY_WIN32_BEGIN","<!--");
				obj.set("ONLY_WIN32_END","-->");
				#endif //_WIN32

				ret.set("settings", obj);

				getArchiveSettings(ret, db, t_clientid);
			}
		}
		if(sa==L"mail_save" && helper.getRights("mail_settings")=="all")
		{
			updateMailSettings(GET, db);
			ret.set("saved_ok", true);
			std::wstring testmailaddr=GET[L"testmailaddr"];
			if(!testmailaddr.empty())
			{
				MailServer mail_server=BackupServerGet::getMailServerSettings();
				if(url_fak!=NULL)
				{
					std::vector<std::string> to;
					to.push_back(Server->ConvertToUTF8(testmailaddr));
					std::string errmsg;
					bool b=url_fak->sendMail(mail_server, to, "UrBackup mail test", "This is a test mail from UrBackup", &errmsg);
					if(!b)
					{
						ret.set("mail_test", errmsg);
					}
					else
					{
						ret.set("mail_test", "ok");
					}
				}
				else
				{
					ret.set("mail_test", "Mail module not loaded");
				}
			}
			sa=L"mail";
		}
		if( sa==L"mail" && helper.getRights("mail_settings")=="all")
		{
			JSON::Object obj;
			getMailSettings(obj, db);
			ret.set("settings", obj);
			ret.set("sa", sa);
		}
	}
	else
	{
		ret.set("error", 1);
	}
#ifdef _WIN32
	ret.set("ONLY_WIN32_BEGIN", "");
	ret.set("ONLY_WIN32_END", "");
#else
	ret.set("ONLY_WIN32_BEGIN", "<!--");
	ret.set("ONLY_WIN32_END", "-->");
#endif

	helper.Write(ret.get(false));
}

#endif //CLIENT_ONLY

