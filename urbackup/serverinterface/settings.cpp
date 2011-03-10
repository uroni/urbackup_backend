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
	return ret;
}

struct SGeneralSettings
{
	SGeneralSettings(void): no_images(false), autoshutdown(false), autoupdate_clients(true), max_sim_backups(10), max_active_clients(100) {}
	std::wstring backupfolder;
	bool no_images;
	bool autoshutdown;
	bool autoupdate_clients;
	int max_sim_backups;
	int max_active_clients;
};

struct SClientSettings
{
	SClientSettings(void) : overwrite(false) {}
	bool overwrite;
};

SGeneralSettings getGeneralSettings(IDatabase *db)
{
	IQuery *q=db->Prepare("SELECT key, value FROM settings WHERE clientid=0");
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
		else if(key==L"autoshutdown" && value==L"true")
			ret.autoshutdown=true;
		else if(key==L"autoupdate_clients" && value==L"false")
			ret.autoupdate_clients=false;
		else if(key==L"max_active_clients")
			ret.max_active_clients=watoi(value);
		else if(key==L"max_sim_backups")
			ret.max_sim_backups=watoi(value);
	}
	return ret;
}

SClientSettings getClientSettings(IDatabase *db, int clientid)
{
	IQuery *q=db->Prepare("SELECT key, value FROM settings WHERE clientid=?");
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
	IQuery *q_get=db->Prepare("SELECT value FROM settings WHERE clientid=0 AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings SET value=? WHERE key=? AND clientid=0");
	IQuery *q_insert=db->Prepare("INSERT INTO settings (key, value, clientid) VALUES (?,?,0)");

	updateSetting(L"backupfolder", settings.backupfolder, q_get, q_update, q_insert);
	updateSetting(L"no_images", settings.no_images?L"true":L"false", q_get, q_update, q_insert);
	updateSetting(L"autoshutdown", settings.autoshutdown?L"true":L"false",  q_get, q_update, q_insert);
	updateSetting(L"autoupdate_clients", settings.autoupdate_clients?L"true":L"false",  q_get, q_update, q_insert);
	updateSetting(L"max_sim_backups", convert(settings.max_sim_backups),  q_get, q_update, q_insert);
	updateSetting(L"max_active_clients", convert(settings.max_active_clients),  q_get, q_update, q_insert);
}

void saveClientSettings(SClientSettings settings, IDatabase *db, int clientid)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings WHERE clientid="+nconvert(clientid)+" AND key=?");
	IQuery *q_update=db->Prepare("UPDATE settings SET value=? WHERE key=? AND clientid="+nconvert(clientid));
	IQuery *q_insert=db->Prepare("INSERT INTO settings (key, value, clientid) VALUES (?,?,"+nconvert(clientid)+")");

	updateSetting(L"overwrite", settings.overwrite?L"true":L"false", q_get, q_update, q_insert);
}

void updateClientSettings(int t_clientid, str_map &GET, IDatabase *db)
{
	IQuery *q_get=db->Prepare("SELECT value FROM settings WHERE key=? AND clientid=?");
	IQuery *q_update=db->Prepare("UPDATE settings SET value=? WHERE key=? AND clientid=?");
	IQuery *q_insert=db->Prepare("INSERT INTO settings (key, value, clientid) VALUES (?,?,?)");

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
	
	IQuery *q_del=db->Prepare("DELETE FROM si_permissions WHERE clientid=?");
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
			IQuery *q_insert=db->Prepare("INSERT INTO si_permissions (t_domain, t_right, clientid) VALUES (?,?,?)");

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
					}
					
					saveClientSettings(s, db, t_clientid);
					if(GET[L"no_ok"]!=L"true")
					{
						ret.set("saved_ok", true);
					}

					ServerSettings::updateAll();
				}
				
				ServerSettings settings(db, t_clientid);
				
				JSON::Object obj=getJSONClientSettings(settings);
				s=getClientSettings(db, t_clientid);
				obj.set("overwrite", s.overwrite);
				obj.set("clientid", t_clientid);
				ret.set("settings",  obj);
				
				sa=L"clientsettings";
				ret.set("sa", sa);
			}
		}
		else if(sa==L"useradd" && helper.getRights("usermod")=="all")
		{
			std::wstring name=strlower(GET[L"name"]);
			std::wstring salt=GET[L"salt"];
			std::wstring pwmd5=GET[L"pwmd5"];

			IQuery *q_find=db->Prepare("SELECT id FROM si_users WHERE name=?");
			q_find->Bind(name);
			db_results res=q_find->Read();
			q_find->Reset();


			if(res.empty())
			{
				IQuery *q=db->Prepare("INSERT INTO si_users (name, password_md5, salt) VALUES (?,?,?)");
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
		if(sa==L"changepw" && helper.getRights("usermod")=="all")
		{
			std::wstring salt=GET[L"salt"];
			std::wstring pwmd5=GET[L"pwmd5"];
			int t_userid=watoi(GET[L"userid"]);
			IQuery *q=db->Prepare("UPDATE si_users SET salt=?, password_md5=? WHERE id=?");
			
			q->Bind(salt);
			q->Bind(pwmd5);
			q->Bind(t_userid);
			q->Write();
			q->Reset();
			ret.set("change_ok", true);
			sa=L"listusers";
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

			IQuery *q=db->Prepare("DELETE FROM si_users WHERE id=?");
			q->Bind(userid);
			q->Write();
			q->Reset();
			ret.set("removeuser", true);
			sa=L"listusers";
		}
		if(sa==L"listusers" && helper.getRights("usermod")=="all" )
		{
			IQuery *q=db->Prepare("SELECT id,name FROM si_users");
			db_results res=q->Read();
			q->Reset();
			q=db->Prepare("SELECT t_right, t_domain FROM si_permissions WHERE clientid=?");
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
				settings.autoshutdown=(GET[L"autoshutdown"]==L"true");
				settings.autoupdate_clients=(GET[L"autoupdate_clients"]==L"true");
				settings.max_active_clients=watoi(GET[L"max_active_clients"]);
				settings.max_sim_backups=watoi(GET[L"max_sim_backups"]);
				updateClientSettings(0, GET, db);
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
				obj.set("autoshutdown", settings.autoshutdown);
				obj.set("autoupdate_clients", settings.autoupdate_clients);
				obj.set("max_sim_backups", settings.max_sim_backups);
				obj.set("max_active_clients", settings.max_active_clients);

				ret.set("settings", obj);
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