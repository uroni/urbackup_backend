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
#include "../../Interface/SettingsReader.h"
#include "../../urlplugin/IUrlFactory.h"
#include "../../urbackupcommon/os_functions.h"
#include "../../cryptoplugin/ICryptoFactory.h"
#include "../../urbackupcommon/settingslist.h"
#include "../ClientMain.h"
#include "../server_archive.h"
#include "../dao/ServerBackupDao.h"
#include "../server.h"

extern IUrlFactory *url_fak;
extern ICryptoFactory *crypto_fak;

void updateRights(int t_userid, std::string s_rights, IDatabase *db)
{
	str_map rights;
	ParseParamStrHttp(s_rights, &rights);

	IQuery *q_del=db->Prepare("DELETE FROM settings_db.si_permissions WHERE clientid=?");
	q_del->Bind(t_userid);
	q_del->Write();
	q_del->Reset();

	str_map::iterator idx=rights.find("idx");

	if(idx!=rights.end())
	{
		std::vector<std::string> elms;
		Tokenize(idx->second, elms, ",");

		if(!elms.empty())
		{
			IQuery *q_insert=db->Prepare("INSERT INTO settings_db.si_permissions (t_domain, t_right, clientid) VALUES (?,?,?)");

			for(size_t i=0;i<elms.size();++i)
			{
				str_map::iterator it_domain=rights.find(elms[i]+"_domain");
				str_map::iterator it_right=rights.find(elms[i]+"_right");
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

namespace 
{

std::vector<std::string> getMailSettingsList(void)
{
	std::vector<std::string> tmp;
	tmp.push_back("mail_servername");
	tmp.push_back("mail_serverport");
	tmp.push_back("mail_username");
	tmp.push_back("mail_password");
	tmp.push_back("mail_from");
	tmp.push_back("mail_ssl_only");
	tmp.push_back("mail_check_certificate");
	tmp.push_back("mail_use_smtps");
	tmp.push_back("mail_admin_addrs");
	return tmp;
}

JSON::Array getAlertScripts(IDatabase* db)
{
	db_results res_scripts = db->Read("SELECT id, name FROM alert_scripts");

	IQuery* q_get_params = db->Prepare("SELECT name, label, default_value, has_translation, type FROM alert_script_params WHERE script_id=? ORDER BY idx ASC");
	JSON::Array ret;
	for (size_t i = 0; i < res_scripts.size(); ++i)
	{
		q_get_params->Bind(res_scripts[i]["id"]);
		db_results res_params = q_get_params->Read();
		q_get_params->Reset();

		JSON::Object script;
		script.set("id", res_scripts[i]["id"]);
		script.set("name", res_scripts[i]["name"]);
		
		JSON::Array params;
		for (size_t j = 0; j < res_params.size(); ++j)
		{
			JSON::Object obj;
			obj.set("name", res_params[j]["name"]);
			obj.set("label", res_params[j]["label"]);
			obj.set("default_value", res_params[j]["default_value"]);
			obj.set("has_translation", watoi(res_params[j]["has_translation"]));
			obj.set("type", res_params[j]["type"]);
			params.add(obj);
		}
		script.set("params", params);

		ret.add(script);
	}

	return ret;
}

JSON::Value addNextArchival(IDatabase* db, int clientid, IQuery* get_next, const JSON::Value& archive_val)
{
	if (archive_val.getType() != JSON::str_type)
		return archive_val;

	std::string archive_str = archive_val.getString();

	str_map params;
	ParseParamStrHttp(archive_str, &params);

	for (str_map::iterator it = params.begin(); it != params.end(); ++it)
	{
		if (next(it->first, 0, "uuid_"))
		{
			std::string idx = getafter("uuid_", it->first);
			std::string buuid = hexToBytes(it->second);

			get_next->Bind(clientid);
			get_next->Bind(buuid.data(), buuid.size());
			db_results res = get_next->Read();
			get_next->Reset();

			if (!res.empty())
			{
				int64 next_archival = watoi64(res[0]["next_archival"]);
				archive_str += "&next_archival_" + idx + "=" + res[0]["next_archival"];
				archive_str += "&timeleft_" + idx + "=" + convert(next_archival - (_i64)Server->getTimeSeconds());
			}
		}
	}

	return JSON::Value(archive_str);
}


JSON::Object getJSONClientSettings(IDatabase* db, int t_clientid)
{
	std::map<std::string, ServerSettings::SClientSetting> settings = ServerSettings::getClientSettings(db, t_clientid);

	IQuery* get_next = db->Prepare("SELECT next_archival FROM settings_db.automatic_archival WHERE clientid=? AND uuid=?");

	JSON::Object ret;
	for (std::map<std::string, ServerSettings::SClientSetting>::iterator it = settings.begin();
		it != settings.end(); ++it)
	{
		if (it->first == "archive")
		{
			it->second.value = addNextArchival(db, t_clientid, get_next, it->second.value);
			it->second.value_client = addNextArchival(db, t_clientid, get_next, it->second.value_client);
			it->second.value_group = addNextArchival(db, t_clientid, get_next, it->second.value_group);
		}

		JSON::Object jobj;
		if (it->second.use != -1)
			jobj.set("use", it->second.use);
		if (it->second.value.getType() != JSON::null_type)
			jobj.set("value", it->second.value);
		if (it->second.value_client.getType() != JSON::null_type)
			jobj.set("value_client", it->second.value_client);
		if (it->second.value_group.getType() != JSON::null_type)
			jobj.set("value_group", it->second.value_group);

		ret.set(it->first, jobj);
	}

	return ret;
}

void getGeneralSettings(JSON::Object& obj, IDatabase *db, ServerSettings &settings)
{
	std::auto_ptr<ISettingsReader> settings_db(Server->createDBSettingsReader(db, "settings_db.settings",
		"SELECT value FROM settings_db.settings WHERE key=? AND clientid=0"));
#define SET_SETTING(x) obj.set(#x, settings.getSettings()->x);
#define SET_SETTING_DB(x, def) obj.set(#x, settings_db->getValue(#x, (def)))
#define SET_SETTING_DB_BOOL(x, def) obj.set(#x, settings_db->getValue(#x, convert(def))=="true")

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
	SET_SETTING(internet_server_proxy);
	SET_SETTING(global_local_speed);
	SET_SETTING(global_soft_fs_quota);
	SET_SETTING(global_internet_speed);
	SET_SETTING(use_tmpfiles);
	SET_SETTING(use_tmpfiles_images);
	SET_SETTING(tmpdir);
	SET_SETTING(update_stats_cachesize);
	SET_SETTING(use_incremental_symlinks);
	SET_SETTING(show_server_updates);
	SET_SETTING(server_url);
	SET_SETTING_DB_BOOL(update_dataplan_db, true);
	SET_SETTING_DB(restore_authkey, std::string());
	SET_SETTING_DB(internet_expect_endpoint, std::string());
	SET_SETTING_DB(internet_server_bind_port, std::string());
#undef SET_SETTING
#undef SET_SETTING_DB
}

void getMailSettings(JSON::Object &obj, IDatabase *db)
{
	std::vector<std::string> slist=getMailSettingsList();
	IQuery *q=db->Prepare("SELECT key, value FROM settings_db.settings WHERE clientid=0 AND key=?");
	for(size_t i=0;i<slist.size();++i)
	{
		q->Bind(slist[i]);
		db_results res=q->Read();
		q->Reset();
		if(!res.empty())
		{
			obj.set((slist[i]), res[0]["value"]);
		}
		else
		{
			std::string v="";
			if(slist[i]=="mail_serverport")
				v="25";
			obj.set((slist[i]), v);
		}
	}
}

void getLdapSettings(JSON::Object &obj, IDatabase *db, ServerSettings &settings)
{
	SLDAPSettings ldap_settings = settings.getLDAPSettings();
#define SET_SETTING(x) obj.set("ldap_" #x, ldap_settings.x);

	SET_SETTING(login_enabled);
	SET_SETTING(server_name);
	SET_SETTING(server_port);
	SET_SETTING(username_prefix);
	SET_SETTING(username_suffix);
	SET_SETTING(group_class_query);
	SET_SETTING(group_key_name);
	SET_SETTING(class_key_name);
	obj.set("ldap_group_rights_map", settings.ldapMapToString(ldap_settings.group_rights_map));
	obj.set("ldap_class_rights_map", settings.ldapMapToString(ldap_settings.class_rights_map));
#undef SET_SETTING
}

void updateSetting(const std::string &key, const std::string &value, IQuery *q_get, IQuery *q_update, IQuery *q_insert, int* use=NULL, int64* use_last_modified=NULL)
{
	q_get->Bind(key);
	db_results r_get=q_get->Read();
	q_get->Reset();

	bool use_mod = false;

	if (!r_get.empty()
		&& use!=NULL)
	{
		int64 old_use_last_mod = watoi64(r_get[0]["use_last_modified"]);
		int old_use = watoi(r_get[0]["use"]);

		if (use_last_modified!=NULL &&
			old_use_last_mod > *use_last_modified)
		{
			*use_last_modified = old_use_last_mod+1;
		}

		if (*use != c_use_undefined &&
			old_use != *use)
		{
			use_mod = true;
		}
		else if (*use == c_use_undefined)
		{
			*use = old_use;
		}
	}

	if(r_get.empty())
	{
		q_insert->Bind(key);
		q_insert->Bind(value);
		if (use != NULL)
			q_insert->Bind(*use);
		if (use_last_modified != NULL)
			q_insert->Bind(*use_last_modified);
		q_insert->Write();
		q_insert->Reset();
	}
	else if( r_get[0]["value"]!=value 
		|| use_mod)
	{
		q_update->Bind(value);
		if (use != NULL)
			q_update->Bind(*use);
		if (use_last_modified != NULL)
			q_update->Bind(*use_last_modified);
		q_update->Bind(key);
		q_update->Write();
		q_update->Reset();
	}
}

namespace
{
	void updateSetting(const std::string &key, const std::string &value, IQuery *q_get, IQuery *q_update, IQuery *q_insert, int clientid)
	{
		q_get->Bind(key);
		q_get->Bind(clientid);
		db_results r_get=q_get->Read();
		q_get->Reset();
		if(r_get.empty())
		{
			q_insert->Bind(key);
			q_insert->Bind(value);
			q_insert->Bind(clientid);
			q_insert->Write();
			q_insert->Reset();
		}
		else if( r_get[0]["value"]!=value )
		{
			q_update->Bind(value);
			q_update->Bind(key);
			q_update->Bind(clientid);
			q_update->Write();
			q_update->Reset();
		}
	}

	std::string fixupBackupfolder(const std::string val, ServerBackupDao& backupdao, ServerSettings &server_settings, bool& changed_backupfolder)
	{
		if(val!=server_settings.getSettings()->backupfolder)
		{
			backupdao.addToOldBackupfolders(server_settings.getSettings()->backupfolder);
			changed_backupfolder = true;
		}

		if(val.find_last_of(os_file_sep())==val.size()-os_file_sep().size()
			&& val.size()>1)
		{
			return val.substr(0, val.size()-os_file_sep().size());
		}
		else
		{
			return val;
		}
	}
}

void saveGeneralSettings(str_map &POST, IDatabase *db, ServerBackupDao& backupdao, ServerSettings &server_settings, bool& changed_backupfolder)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,0)");

	std::vector<std::string> settings=getGlobalSettingsList();
	for(size_t i=0;i<settings.size();++i)
	{
		str_map::iterator it=POST.find(settings[i]);
		if(it!=POST.end())
		{
			std::string val = UnescapeSQLString(it->second);
			if(settings[i]=="backupfolder")
			{
				val = fixupBackupfolder(val, backupdao, server_settings, changed_backupfolder);

#ifndef _WIN32
				os_create_dir("/etc/urbackup");
				writestring((val), "/etc/urbackup/backupfolder");
#endif
			}
			updateSetting(settings[i], val, q_get, q_update, q_insert);
		}
	}

#ifdef _WIN32
	std::string tmpdir=POST["tmpdir"];
	if(!tmpdir.empty())
	{
		os_create_dir(tmpdir+os_file_sep()+"urbackup_tmp");
		Server->setTemporaryDirectory(tmpdir+os_file_sep()+"urbackup_tmp");
	}
#endif
}

void updateSettingsWithList(str_map &POST, IDatabase *db, const std::vector<std::string>& settingsList)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings_db.settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,0)");

	for(size_t i=0;i<settingsList.size();++i)
	{
		str_map::iterator it=POST.find(settingsList[i]);
		if(it!=POST.end())
		{
			updateSetting(settingsList[i], UnescapeSQLString(it->second), q_get, q_update, q_insert);
		}
	}
}

void updateClientGroup(int t_clientid, int groupid, IDatabase *db)
{
	IQuery *q_get = db->Prepare("SELECT value FROM settings_db.settings WHERE key=? AND clientid=" + convert(t_clientid));
	IQuery *q_update = db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=" + convert(t_clientid));
	IQuery *q_insert = db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?," + convert(t_clientid) + ")");

	updateSetting("group_id", convert(groupid), q_get, q_update, q_insert);

	IQuery* q = db->Prepare("UPDATE clients SET groupid=? WHERE id=?");
	q->Bind(groupid);
	q->Bind(t_clientid);
	q->Write();
	q->Reset();
}

void archiveParamsSetUuid(str_map& POST)
{
	std::string archive = UnescapeSQLString(POST["archive"]);

	str_map params;
	ParseParamStrHttp(archive, &params);

	bool mod = false;
	std::vector<std::pair<std::string, std::string> > new_uuids;
	for (str_map::iterator it = params.begin(); it != params.end(); ++it)
	{
		if (next(it->first, 0, "uuid_"))
		{
			if (it->second.size() % 2 != 0
				|| !IsHex(it->second))
			{
				std::string uuid;
				uuid.resize(16);
				Server->secureRandomFill(&uuid[0], uuid.size());
				it->second = bytesToHex(uuid);
				mod = true;
			}
		}
		else if (next(it->first, 0, "backup_type_"))
		{
			std::string idx = getafter("backup_type_", it->first);

			if (params.find("uuid_" + idx) == params.end())
			{
				std::string uuid;
				uuid.resize(16);
				Server->secureRandomFill(&uuid[0], uuid.size());
				new_uuids.push_back(std::make_pair("uuid_" + idx, uuid));
				mod = true;
			}
		}
	}

	if (!mod)
		return;

	for (size_t i = 0; i < new_uuids.size(); ++i)
	{
		params[new_uuids[i].first] = bytesToHex(new_uuids[i].second);
	}

	archive = "";

	for (str_map::iterator it = params.begin(); it != params.end(); ++it)
	{
		if (!archive.empty())
			archive += "&";
		archive += it->first + "=" + EscapeParamString(it->second);
	}

	POST["archive"] = EscapeSQLString(archive);
}

void updateClientSettings(int t_clientid, str_map &POST, IDatabase *db)
{
	archiveParamsSetUuid(POST);

	IQuery *q_get=db->Prepare("SELECT value, use, use_last_modified FROM settings_db.settings WHERE key=? AND clientid="+convert(t_clientid));
	IQuery *q_update=db->Prepare("UPDATE settings_db.settings SET value=?, use=?, use_last_modified=? WHERE key=? AND clientid="+convert(t_clientid));
	IQuery *q_insert=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid, use, use_last_modified) VALUES (?,?,"+convert(t_clientid)+", ?, ?)");

	int64 ctime = Server->getTimeSeconds();

	std::vector<std::string> sset_client_merge = getClientMergableSettingsList();
	std::sort(sset_client_merge.begin(), sset_client_merge.end());
	std::vector<std::string> sset_client_use = getClientConfigurableSettingsList();
	std::sort(sset_client_use.begin(), sset_client_use.end());
	std::vector<std::string> sset_localized = getLocalizedSettingsList();
	std::sort(sset_localized.begin(), sset_localized.end());

	std::vector<std::string> sset=getSettingsList();
	for(size_t i=0;i<sset.size();++i)
	{
		str_map::iterator it=POST.find(sset[i]);
		str_map::iterator it_used = POST.find(sset[i] + ".use");
		if(it!=POST.end())
		{
			int use = c_use_value;
			if (it_used != POST.end())
				use = watoi(it_used->second);

			if ( use != c_use_value && use!=c_use_group && use != (c_use_value|c_use_group)
				&& !std::binary_search(sset_client_use.begin(), sset_client_use.end(), sset[i]))
			{
				use = c_use_value;
			}

			if ( (use!=c_use_group && use!=c_use_value && use!=c_use_value_client)
				&& !std::binary_search(sset_client_merge.begin(), sset_client_merge.end(), sset[i]))
			{
				use = c_use_value;
			}

			if (use != c_use_value && std::binary_search(sset_localized.begin(), sset_localized.end(), sset[i]))
				use = c_use_value;

			int64 use_last_modified = ctime;
			updateSetting(sset[i], UnescapeSQLString(it->second), q_get, q_update, q_insert, &use, &use_last_modified);
			
			q_get->Bind(sset[i]);
			db_results res = q_get->Read();
			q_get->Reset();
		}
	}
}

void updateArchiveSettings(int clientid, IDatabase *db)
{
	IQuery* q = db->Prepare("INSERT INTO settings_db.settings(key, value, clientid) VALUES ('archive_update', '1', ?)");
	if (clientid <= 0)
	{
		db_results res_ids = db->Read("SELECT id FROM clients");

		for (size_t i = 0; i < res_ids.size(); ++i)
		{
			q->Bind(res_ids[i]["id"]);
			q->Write();
			q->Reset();
		}
	}
	else
	{
		q->Bind(clientid);
		q->Write();
	}
}

void updateOnlineClientSettings(IDatabase *db, int clientid)
{
	IQuery *q=db->Prepare("SELECT name FROM clients WHERE id=?");
	q->Bind(clientid);
	db_results res = q->Read();
	q->Reset();
	if(!res.empty())
	{
		std::string clientname = res[0]["name"];

		IPipe* comm_pipe = ServerStatus::getStatus(clientname).comm_pipe;
		if(comm_pipe!=NULL)
		{
			comm_pipe->Write("UPDATE SETTINGS");
		}
	}
}

void updateAllOnlineClientSettings(IDatabase *db)
{
	IQuery *q=db->Prepare("SELECT name FROM clients");
	db_results res = q->Read();
	for(size_t i=0;i<res.size();++i)
	{
		std::string clientname = res[i]["name"];

		IPipe* comm_pipe = ServerStatus::getStatus(clientname).comm_pipe;
		if(comm_pipe!=NULL)
		{
			comm_pipe->Write("UPDATE SETTINGS");
		}
	}
}

}

ACTION_IMPL(settings)
{
	Helper helper(tid, &POST, &PARAMS);
	JSON::Object ret;
	SUser *session=helper.getSession();
	if(session!=NULL && session->id==SESSION_ID_INVALID) return;
	std::string sa=POST["sa"];
	int t_clientid=watoi(POST["t_clientid"]);
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
			sa="general";
		}
		else if(helper.getRights("usermod")=="all")
		{
			sa="listusers";
		}
		else if(!clientid.empty() )
		{
			sa="clientsettings";
			t_clientid=clientid[0];
		}
	}

	if(session!=NULL && helper.getRights("settings")!="none")
	{
		if (t_clientid <= 0
			&& sa == "clientsettings_save")
		{
			std::string group_mem_changes = POST["group_mem_changes"];
			if (!group_mem_changes.empty()
				&& helper.getRights("groupmod") == RIGHT_ALL)
			{
				std::vector<std::string> changes;
				Tokenize(group_mem_changes, changes, ";");
				for (size_t i = 0; i < changes.size(); ++i)
				{
					int mod_clientid = watoi(getuntil("-", changes[i]));
					int mod_groupid = watoi(getafter("-", changes[i]));

					updateClientGroup(mod_clientid, mod_groupid, db);
				}
			}
		}
		else if (t_clientid > 0
			&& sa == "clientsettings_save"
			&& helper.getRights("groupmod") == RIGHT_ALL
			&& POST.find("memberof") != POST.end())
		{
			updateClientGroup(t_clientid, watoi(POST["memberof"]), db);
		}
		else if (sa == "groupremove" && helper.getRights("groupmod") == RIGHT_ALL)
		{
			DBScopedWriteTransaction remove_transaction(db);

			int groupid = watoi(POST["id"]);
			IQuery* q1 = db->Prepare("UPDATE clients SET groupid=0 WHERE groupid=?");
			q1->Bind(groupid);
			q1->Write();
			q1->Reset();

			IQuery* q2 = db->Prepare("UPDATE settings_db.settings SET value='0' WHERE key='group_id' AND value=?");
			q2->Bind(groupid);
			q2->Write();
			q2->Reset();

			IQuery* q3 = db->Prepare("DELETE FROM settings_db.si_client_groups WHERE id=?");
			q3->Bind(groupid);
			q3->Write();
			q3->Reset();

			IQuery* q4 = db->Prepare("DELETE FROM settings_db.settings WHERE clientid = ?");
			q4->Bind(groupid*-1);
			q4->Write();
			q4->Reset();

			IQuery* q5 = db->Prepare("DELETE FROM settings_db.automatic_archival WHERE clientid = ?");
			q5->Bind(groupid*-1);
			q5->Write();
			q5->Reset();

			remove_transaction.end();

			ServerSettings::updateAll();

			ret.set("delete_ok", true);
			sa = "general";
		}

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
			if(helper.getRights("ldap_settings")=="all" && url_fak!=NULL )
			{
				navitems.set("ldap", true);
			}
			if(crypto_fak!=NULL)
			{
				navitems.set("internet", true);
			}
			if(helper.getRights("disable_change_pw")=="all")
			{
				navitems.set("disable_change_pw", true);
			}
			if (helper.getRights("groupmod") == "all")
			{
				navitems.set("groupmod", true);
			}

			JSON::Array clients;

			IQuery *q_has_overwrite = db->Prepare("SELECT 1 FROM settings_db.settings WHERE clientid=? AND key!='internet_authkey' AND key!='client_access_key' AND key!='computername' AND key!='group_id' AND key!='group_name' AND ( (use & 2)>0 OR (use & 4)>0 ) LIMIT 1");
			
			const std::string clients_tab = " (clients c LEFT OUTER JOIN settings_db.si_client_groups cg ON "
				"c.groupid = cg.id) ";
			if(rights=="all" )
			{
				IQuery *q=db->Prepare("SELECT c.id AS clientid, c.name AS clientname, groupid, cg.name AS groupname FROM " + clients_tab
					 + "ORDER BY c.groupid, c.name");
				
				db_results res=q->Read();
				q->Reset();
				for(size_t i=0;i<res.size();++i)
				{
					JSON::Object u;

					int lclientid = watoi(res[i]["clientid"]);
					ServerSettings client_settings(db, lclientid);

					u.set("id", lclientid);
					u.set("name", res[i]["clientname"]);
					u.set("group", watoi(res[i]["groupid"]));
					u.set("groupname", res[i]["groupname"]);

					q_has_overwrite->Bind(lclientid);
					db_results res_overwrite = q_has_overwrite->Read();
					q_has_overwrite->Reset();

					u.set("override", !res_overwrite.empty());
					clients.add(u);
				}
			}
			else
			{
				std::map<int, db_single_result> group_res;
				IQuery *q=db->Prepare("SELECT c.id AS clientid, c.name AS clientname, groupid, cg.name AS groupname FROM "+ clients_tab + " WHERE c.id=?");
				for(size_t i=0;i<clientid.size();++i)
				{
					q->Bind(clientid[i]);
					db_results res=q->Read();
					q->Reset();
					if (!res.empty())
					{
						group_res[watoi(res[0]["groupid"])] = res[0];
					}
				}
				for (std::map<int, db_single_result>::iterator it = group_res.begin();
					it != group_res.end(); ++it)
				{
					JSON::Object u;

					int lclientid = watoi(it->second["clientid"]);
					ServerSettings client_settings(db, lclientid);
					u.set("id", lclientid);
					u.set("name", it->second["name"]);
					u.set("group", it->first);
					u.set("groupname", it->second["groupname"]);

					q_has_overwrite->Bind(lclientid);
					db_results res_overwrite = q_has_overwrite->Read();
					q_has_overwrite->Reset();

					u.set("override", !res_overwrite.empty());
					clients.add(u);
				}
			}
			
			navitems.set("clients", clients);

			{
				JSON::Array groups;

				JSON::Object u;
				u.set("id", 0);
				u.set("name", "");
				groups.add(u);

				db_results res = db->Read("SELECT id, name FROM settings_db.si_client_groups ORDER BY name");
				for (size_t i = 0; i<res.size(); ++i)
				{
					JSON::Object u;
					u.set("id", watoi(res[i]["id"]));
					u.set("name", res[i]["name"]);
					groups.add(u);
				}

				navitems.set("groups", groups);
			}

			if(helper.getRights("general_settings")=="all" )
			{
				navitems.set("general", true);
			}

			ret.set("navitems", navitems);
		}

		if (sa == "groupadd" && helper.getRights("groupmod") == RIGHT_ALL)
		{
			std::string name_lower = strlower(POST["name"]);
			IQuery* q_find = db->Prepare("SELECT id FROM settings_db.si_client_groups WHERE name=?");
			q_find->Bind(name_lower);
			db_results res = q_find->Read();
			q_find->Reset();

			if (res.empty())
			{
				IQuery *q = db->Prepare("INSERT INTO settings_db.si_client_groups (name) VALUES (?)");
				q->Bind(POST["name"]);
				q->Write();
				q->Reset();

				ret.set("add_ok", true);
				JSON::Object u;
				int groupid = static_cast<int>(db->getLastInsertID());
				u.set("id", groupid);
				u.set("name", POST["name"]);
				ret.set("added_group", u);

				sa = "clientsettings";
				t_clientid = -1 * groupid;

				q = db->Prepare("DELETE FROM settings_db.settings WHERE clientid = ?");
				q->Bind(t_clientid);
				q->Write();
				q->Reset();

				q = db->Prepare("INSERT INTO settings_db.settings (key, value, value_client, clientid, use, use_last_modified) SELECT key, value, value_client, ? AS clientid, " +convert(c_use_group)+ ", 0 FROM settings_db.settings WHERE clientid = 0");
				q->Bind(t_clientid);
				q->Write();
				q->Reset();

				q = db->Prepare("DELETE FROM settings_db.automatic_archival WHERE clientid = ?");
				q->Bind(t_clientid);
				q->Write();
				q->Reset();

				q = db->Prepare("INSERT INTO settings_db.automatic_archival (clientid, interval, interval_unit, length, length_unit, backup_types, archive_window, letters) "
					"SELECT ? AS clientid, interval, interval_unit, length, length_unit, backup_types, archive_window, letters FROM settings_db.automatic_archival WHERE clientid=0");
				q->Bind(t_clientid);
				q->Write();
				q->Reset();

				ServerSettings::updateAll();
			}
			else
			{
				ret.set("already_exists", true);
			}
		}

		if(sa=="clientsettings" || sa=="clientsettings_save")
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
				if (sa == "clientsettings_save")
				{
					db->BeginWriteTransaction();
					updateClientSettings(t_clientid, POST, db);
					if (POST["no_ok"] != "true")
					{
						updateArchiveSettings(t_clientid, db);
					}
					db->EndTransaction();

					if (POST["no_ok"] != "true")
					{
						ret.set("saved_ok", true);
					}
					else
					{
						ret.set("saved_part", true);
					}

					if (t_clientid > 0)
					{
						ServerSettings::updateClient(t_clientid);
					}
					else
					{
						ServerSettings::updateAll();
					}				

					updateOnlineClientSettings(db, t_clientid);
				}
				
				ServerSettings settings(db, t_clientid);
				
				JSON::Object obj=getJSONClientSettings(db, t_clientid);
				obj.set("clientid", t_clientid);
				obj.set("alert_scripts", getAlertScripts(db));
				if (helper.getRights(RIGHT_ALERT_SCRIPTS) == RIGHT_ALL)
				{
					obj.set("can_edit_scripts", true);
				}
				if (t_clientid <= 0)
				{
					obj.set("groupid", t_clientid*-1);
					obj.set("groupname", backupdao.getGroupName(t_clientid*-1).value);
					obj.set("overwrite", true);
				}
				else
				{
					ServerBackupDao::SClientName client_name = backupdao.getVirtualMainClientname(t_clientid);
					obj.set("main_client", client_name.virtualmain.empty());
					obj.set("clientname", client_name.name);
					obj.set("memberof", backupdao.getClientGroup(t_clientid).value);
				}

				ret.set("cowraw_available", BackupServer::isImageSnapshotsEnabled()
					|| BackupServer::canReflink() );
				ret.set("settings",  obj);
				
				sa="clientsettings";
				ret.set("sa", sa);
			}
		}
		else if(sa=="useradd" && helper.getRights("usermod")=="all")
		{
			std::string name=strlower(POST["name"]);
			std::string salt=POST["salt"];
			std::string pwmd5=(POST["pwmd5"]);

			IQuery *q_find=db->Prepare("SELECT id FROM settings_db.si_users WHERE name=?");
			q_find->Bind(name);
			db_results res=q_find->Read();
			q_find->Reset();

			if(res.empty())
			{
				size_t pbkdf2_rounds=0;
				if(crypto_fak!=NULL)
				{
					pbkdf2_rounds=10000;

					pwmd5 = strlower(crypto_fak->generatePasswordHash(hexToBytes(pwmd5), (salt), pbkdf2_rounds));
				}

				IQuery *q=db->Prepare("INSERT INTO settings_db.si_users (name, password_md5, salt, pbkdf2_rounds) VALUES (?,?,?,?)");
				q->Bind(name);
				q->Bind(pwmd5);
				q->Bind(salt);
				q->Bind(pbkdf2_rounds);
				q->Write();
				q->Reset();

				ret.set("add_ok", true);

				int t_userid=(int)db->getLastInsertID();

				std::string s_rights = POST["rights"];
				updateRights(t_userid, s_rights, db);
			}
			else
			{
				ret.set("alread_exists", true);
			}			

			sa="listusers";
		}
		
		if(sa=="changepw" && ( helper.getRights("usermod")=="all" || (POST["userid"]=="own" && helper.getRights("disable_change_pw")!="all") ) )
		{
			bool ok=true;
			if(POST["userid"]=="own")
			{
				if(!helper.checkPassword(session->mStr["username"], POST["old_pw"], NULL, false) )
				{
					ok=false;
				}
			}
			if(ok)
			{
				std::string salt=POST["salt"];
				std::string pwmd5=(POST["pwmd5"]);
				int t_userid;
				if(POST["userid"]=="own")
				{
					t_userid=session->id;
				}
				else
				{
					t_userid=watoi(POST["userid"]);
				}

				size_t pbkdf2_rounds=0;
				if(crypto_fak!=NULL)
				{
					pbkdf2_rounds = default_pbkdf2_rounds;

					pwmd5 = strlower(crypto_fak->generatePasswordHash(hexToBytes(pwmd5), (salt), pbkdf2_rounds));
				}

				IQuery *q=db->Prepare("UPDATE settings_db.si_users SET salt=?, password_md5=?, pbkdf2_rounds=? WHERE id=?");
			
				q->Bind(salt);
				q->Bind(pwmd5);
				q->Bind(pbkdf2_rounds);
				q->Bind(t_userid);
				q->Write();
				q->Reset();
				ret.set("change_ok", true);

				if(POST["userid"]!="own")
				{
					sa="listusers";
				}
			}
			else
			{
				ret.set("old_pw_wrong", true);
			}
		}
		if(sa=="updaterights" && helper.getRights("usermod")=="all")
		{
			int t_userid=watoi(POST["userid"]);
			std::string s_rights=(POST["rights"]);
			updateRights(t_userid, s_rights, db);
			sa="listusers";
			ret.set("update_right", true);
		}
		if(sa=="removeuser" && helper.getRights("usermod")=="all")
		{
			int userid=watoi(POST["userid"]);

			IQuery *q=db->Prepare("DELETE FROM settings_db.si_users WHERE id=?");
			q->Bind(userid);
			q->Write();
			q->Reset();
			ret.set("removeuser", true);
			sa="listusers";
		}
		if(sa=="listusers" && helper.getRights("usermod")=="all" )
		{
			IQuery *q=db->Prepare("SELECT id,name FROM settings_db.si_users");
			db_results res=q->Read();
			q->Reset();
			q=db->Prepare("SELECT t_right, t_domain FROM settings_db.si_permissions WHERE clientid=?");
			JSON::Array users;
			for(size_t i=0;i<res.size();++i)
			{
				JSON::Object obj;
				obj.set("name", res[i]["name"]);
				obj.set("id", res[i]["id"]);
				
				q->Bind(res[i]["id"]);
				db_results res_r=q->Read();
				q->Reset();

				JSON::Array rights;
				for(size_t j=0;j<res_r.size();++j)
				{
					JSON::Object o;
					o.set("right", res_r[j]["t_right"]);
					o.set("domain", res_r[j]["t_domain"]);
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
			if(sa=="general_save")
			{
				ServerSettings serv_settings(db);
				db->BeginWriteTransaction();
				updateClientSettings(0, POST, db);
				updateArchiveSettings(0, db);
				bool changed_backupfolder = false;
				saveGeneralSettings(POST, db, backupdao, serv_settings, changed_backupfolder);
				db->EndTransaction();

				ServerSettings::updateAll();

				updateAllOnlineClientSettings(db);

				if (changed_backupfolder)
				{
					BackupServer::testFilesystemThread();
				}

				ret.set("saved_ok", true);
				sa="general";
			}
			if((sa=="navitems" || sa=="general") )
			{
				sa="general";
				ret.set("sa", sa);

				JSON::Object obj=getJSONClientSettings(db, 0);
				obj.set("alert_scripts", getAlertScripts(db));
				if (helper.getRights(RIGHT_ALERT_SCRIPTS) == RIGHT_ALL)
				{
					obj.set("can_edit_scripts", true);
				}
				ServerSettings serv_settings(db, 0);
				getGeneralSettings(obj, db, serv_settings);
				#ifdef _WIN32
				obj.set("ONLY_WIN32_BEGIN","");
				obj.set("ONLY_WIN32_END","");
				#else
				obj.set("ONLY_WIN32_BEGIN","<!--");
				obj.set("ONLY_WIN32_END","-->");
				#endif //_WIN32

				ret.set("cowraw_available", BackupServer::isImageSnapshotsEnabled()
					|| BackupServer::canReflink() );
				ret.set("settings", obj);
			}
		}
		if(sa=="mail_save" && helper.getRights("mail_settings")=="all")
		{
			updateSettingsWithList(POST, db, getMailSettingsList());
			ret.set("saved_ok", true);
			std::string testmailaddr=POST["testmailaddr"];
			if(!testmailaddr.empty())
			{
				MailServer mail_server=ClientMain::getMailServerSettings();
				if(url_fak!=NULL)
				{
					std::vector<std::string> to;
					to.push_back((testmailaddr));
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
			sa="mail";
		}
		if( sa=="mail" && helper.getRights("mail_settings")=="all")
		{
			JSON::Object obj;
			getMailSettings(obj, db);
			ret.set("settings", obj);
			ret.set("sa", sa);
		}
		if(sa=="ldap_save" && helper.getRights("ldap_settings")=="all")
		{
			updateSettingsWithList(POST, db, getLdapSettingsList());
			ret.set("saved_ok", true);
			sa="ldap";

			std::string testusername = POST["testusername"];

			if(!testusername.empty() && helper.ldapEnabled())
			{
				std::string testpassword = POST["testpassword"];

				std::string errmsg;
				std::string rights;
				if(!helper.ldapLogin(testusername, testpassword, &errmsg, &rights, true))
				{
					if(errmsg.empty())
					{
						errmsg="unknown";
					}
					ret.set("ldap_test", errmsg);
				}
				else
				{
					ret.set("ldap_test", "ok");
					ret.set("ldap_rights", rights);
				}
			}
			else if(!testusername.empty() && !helper.ldapEnabled())
			{
				ret.set("ldap_test", "Login via LDAP not enabled");
			}
		}
		if( sa=="ldap" && helper.getRights("ldap_settings")=="all")
		{
			JSON::Object obj;
			ServerSettings serv_settings(db);
			getLdapSettings(obj, db, serv_settings);
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

    helper.Write(ret.stringify(false));
}

#endif //CLIENT_ONLY

