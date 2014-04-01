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
#include "../server_settings.h"
#include "../../Interface/SettingsReader.h"
#include "../../urlplugin/IUrlFactory.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../../urbackupcommon/settingslist.h"
#include "../server_get.h"
#include "../server_archive.h"
#include "../dao/ServerBackupDao.h"

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
	tmp.push_back(L"mail_admin_addrs");
	return tmp;
}

JSON::Object getJSONClientSettings(ServerSettings &settings)
{
	JSON::Object ret;
#define SET_SETTING(x) ret.set(#x, settings.getSettings()->x);
	SET_SETTING(update_freq_incr);
	SET_SETTING(update_freq_full);
	SET_SETTING(update_freq_image_full);
	SET_SETTING(update_freq_image_incr);
	SET_SETTING(max_file_incr);
	SET_SETTING(min_file_incr);
	SET_SETTING(max_file_full);
	SET_SETTING(min_file_full);
	SET_SETTING(min_image_incr);
	SET_SETTING(max_image_incr);
	SET_SETTING(min_image_full);
	SET_SETTING(max_image_full);
	SET_SETTING(allow_overwrite);
	SET_SETTING(startup_backup_delay);
	SET_SETTING(backup_window_incr_file);
	SET_SETTING(backup_window_full_file);
	SET_SETTING(backup_window_incr_image);
	SET_SETTING(backup_window_full_image);
	SET_SETTING(computername);
	SET_SETTING(exclude_files);
	SET_SETTING(include_files);
	SET_SETTING(default_dirs);
	SET_SETTING(allow_config_paths);
	SET_SETTING(allow_starting_full_file_backups);
	SET_SETTING(allow_starting_incr_file_backups);
	SET_SETTING(allow_starting_full_image_backups);
	SET_SETTING(allow_starting_incr_image_backups);
	SET_SETTING(allow_pause);
	SET_SETTING(allow_log_view);
	SET_SETTING(image_letters);
	SET_SETTING(internet_authkey);
	SET_SETTING(client_set_settings);
	SET_SETTING(internet_speed);
	SET_SETTING(local_speed);
	SET_SETTING(internet_mode_enabled);
	SET_SETTING(internet_compress);
	SET_SETTING(internet_encrypt);
	SET_SETTING(internet_image_backups);
	SET_SETTING(internet_full_file_backups);
	SET_SETTING(silent_update);
	SET_SETTING(client_quota);
	SET_SETTING(local_full_file_transfer_mode);
	SET_SETTING(internet_full_file_transfer_mode);
	SET_SETTING(local_incr_file_transfer_mode);
	SET_SETTING(internet_incr_file_transfer_mode);
	SET_SETTING(local_image_transfer_mode);
	SET_SETTING(internet_image_transfer_mode);
	SET_SETTING(file_hash_collect_amount);
	SET_SETTING(file_hash_collect_timeout);
	SET_SETTING(file_hash_collect_cachesize);
	SET_SETTING(end_to_end_file_backup_verification);
	SET_SETTING(internet_calculate_filehashes_on_client);
#undef SET_SETTING
	return ret;
}

struct SClientSettings
{
	SClientSettings(void) : overwrite(false) {}
	bool overwrite;
};

void getGeneralSettings(JSON::Object& obj, IDatabase *db, ServerSettings &settings)
{
#define SET_SETTING(x) obj.set(#x, settings.getSettings()->x);

	SET_SETTING(backupfolder);
	SET_SETTING(no_images);
	SET_SETTING(no_file_backups);
	SET_SETTING(autoshutdown);
	SET_SETTING(download_client);
	SET_SETTING(autoupdate_clients);
	SET_SETTING(max_sim_backups);
	SET_SETTING(max_active_clients);
	SET_SETTING(cleanup_window);
	SET_SETTING(backup_database);
	SET_SETTING(internet_server);
	SET_SETTING(internet_server_port);
	SET_SETTING(global_local_speed);
	SET_SETTING(global_soft_fs_quota);
	SET_SETTING(global_internet_speed);
	SET_SETTING(use_tmpfiles);
	SET_SETTING(use_tmpfiles_images);
	SET_SETTING(tmpdir);
	SET_SETTING(update_stats_cachesize);
	SET_SETTING(filescache_type);
	SET_SETTING(filescache_size);
	SET_SETTING(suspend_index_limit);
	SET_SETTING(use_incremental_symlinks);

#undef SET_SETTING
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

namespace
{
	std::wstring fixupBackupfolder(const std::wstring val, ServerBackupDao& backupdao, ServerSettings &server_settings)
	{
		if(val!=server_settings.getSettings()->backupfolder)
		{
			backupdao.addToOldBackupfolders(server_settings.getSettings()->backupfolder);
		}

		if(val.find(os_file_sep())==val.size()-os_file_sep().size())
		{
			return val.substr(0, val.size()-os_file_sep().size());
		}
		else
		{
			return val;
		}
	}
}

void saveGeneralSettings(str_map &GET, IDatabase *db, ServerBackupDao& backupdao, ServerSettings &server_settings)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,0)");

	std::vector<std::wstring> settings=getGlobalSettingsList();
	for(size_t i=0;i<settings.size();++i)
	{
		str_map::iterator it=GET.find(settings[i]);
		if(it!=GET.end())
		{
			std::wstring val = it->second;
			if(settings[i]==L"backupfolder")
			{
				val = fixupBackupfolder(val, backupdao, server_settings);
			}
			updateSetting(settings[i], val, q_get, q_update, q_insert);
		}
	}

#ifdef _WIN32
	std::wstring tmpdir=GET[L"tmpdir"];
	if(!tmpdir.empty())
	{
		os_create_dir(tmpdir+os_file_sep()+L"urbackup_tmp");
		Server->setTemporaryDirectory(tmpdir+os_file_sep()+L"urbackup_tmp");
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

void saveClientSettings(SClientSettings settings, IDatabase *db, int clientid)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid="+nconvert(clientid)+" AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid="+nconvert(clientid));
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,"+nconvert(clientid)+")");

	updateSetting(L"overwrite", settings.overwrite?L"true":L"false", q_get, q_update, q_insert);
}

void updateClientSettings(int t_clientid, str_map &GET, IDatabase *db)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE key=? AND clientid="+nconvert(t_clientid));
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid="+nconvert(t_clientid));
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,"+nconvert(t_clientid)+")");

	std::vector<std::wstring> sset=getSettingsList();
	sset.push_back(L"allow_overwrite");
	for(size_t i=0;i<sset.size();++i)
	{
		str_map::iterator it=GET.find(sset[i]);
		if(it!=GET.end())
		{
			updateSetting(sset[i], it->second, q_get, q_update, q_insert);
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
	std::string rights=helper.getRights("client_settings");
	std::vector<int> clientid;
	IDatabase *db=helper.getDatabase();
	ServerBackupDao backupdao(db);
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

	if(session!=NULL && helper.getRights("settings")!="none")
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
			if(helper.getRights("disable_change_pw")=="all")
			{
				navitems.set("disable_change_pw", true);
			}

			JSON::Array clients;
			
			if(rights=="all" )
			{
				IQuery *q=db->Prepare("SELECT id,name FROM clients ORDER BY name");
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
						db->BeginTransaction();
						updateClientSettings(t_clientid, GET, db);
						updateArchiveSettings(t_clientid, GET, db);
						db->EndTransaction();
					}
					
					db->BeginTransaction();
					saveClientSettings(s, db, t_clientid);
					db->EndTransaction();
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
		if(sa==L"changepw" && ( helper.getRights("usermod")=="all" || (GET[L"userid"]==L"own" && helper.getRights("disable_change_pw")!="all") ) )
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
				ServerSettings serv_settings(db);
				db->BeginTransaction();
				updateClientSettings(0, GET, db);
				updateArchiveSettings(0, GET, db);
				saveGeneralSettings(GET, db, backupdao, serv_settings);
				db->EndTransaction();

				ServerSettings::updateAll();

				ret.set("saved_ok", true);
				sa=L"general";
			}
			if((sa==L"navitems" || sa==L"general") )
			{
				sa=L"general";
				ret.set("sa", sa);

				ServerSettings serv_settings(db);
				JSON::Object obj=getJSONClientSettings(serv_settings);
				getGeneralSettings(obj, db, serv_settings);
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

