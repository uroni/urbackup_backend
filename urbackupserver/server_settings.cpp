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

#include "../stringtools.h"
#include "../urbackupcommon/settingslist.h"
#include <stdlib.h>
#ifndef CLIENT_ONLY

#include "server_settings.h"
#include "../Interface/Server.h"

std::vector<ServerSettings*> ServerSettings::g_settings;
IMutex *ServerSettings::g_mutex=NULL;

void ServerSettings::init_mutex(void)
{
	if(g_mutex==NULL)
		g_mutex=Server->createMutex();
}

void ServerSettings::destroy_mutex(void)
{
	if(g_mutex!=NULL)
	{
		Server->destroy(g_mutex);
	}
}

ServerSettings::ServerSettings(IDatabase *db, int pClientid) : clientid(pClientid)
{
	{
		IScopedLock lock(g_mutex);
		g_settings.push_back(this);
	}

	settings_default=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings_db.settings WHERE key=? AND clientid=0");
	if(clientid!=-1)
	{
		settings_client=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings_db.settings WHERE key=? AND clientid="+nconvert(clientid));
	}
	else
	{
		settings_client=NULL;
	}

	update();
	do_update=false;
}

ServerSettings::~ServerSettings(void)
{
	Server->destroy(settings_default);
	if(settings_client!=NULL)
	{
		Server->destroy(settings_client);
	}

	{
		IScopedLock lock(g_mutex);
		for(size_t i=0;i<g_settings.size();++i)
		{
			if(g_settings[i]==this)
			{
				g_settings.erase(g_settings.begin()+i);
				break;
			}
		}
	}
}

void ServerSettings::updateAll(void)
{
	IScopedLock lock(g_mutex);
	for(size_t i=0;i<g_settings.size();++i)
	{
		g_settings[i]->doUpdate();
	}
}

void ServerSettings::update(void)
{
	readSettingsDefault();
	if(settings_client!=NULL)
	{
		readSettingsClient();
	}
}

void ServerSettings::doUpdate(void)
{
	do_update=true;
}

SSettings *ServerSettings::getSettings(bool *was_updated)
{
	if(do_update)
	{
		if(was_updated!=NULL)
			*was_updated=true;

		do_update=false;
		update();
	}
	else
	{
		if(was_updated!=NULL)
			*was_updated=false;
	}
	return &settings;
}

void ServerSettings::readSettingsDefault(void)
{
	settings.update_freq_incr=settings_default->getValue("update_freq_incr", 5*60*60);
	settings.update_freq_full=settings_default->getValue("update_freq_full", 30*24*60*60);
	settings.update_freq_image_incr=settings_default->getValue("update_freq_image_incr", 7*24*60*60);
	settings.update_freq_image_full=settings_default->getValue("update_freq_image_full", 60*24*60*60);
	settings.max_file_incr=settings_default->getValue("max_file_incr", 100);
	settings.min_file_incr=settings_default->getValue("min_file_incr", 40);
	settings.max_file_full=settings_default->getValue("max_file_full", 10);
	settings.min_file_full=settings_default->getValue("min_file_full", 2);
	settings.min_image_incr=settings_default->getValue("min_image_incr", 4);
	settings.max_image_incr=settings_default->getValue("max_image_incr", 30);
	settings.min_image_full=settings_default->getValue("min_image_full", 2);
	settings.max_image_full=settings_default->getValue("max_image_full", 5);
	settings.no_images=(settings_default->getValue("no_images", "false")=="true");
	settings.no_file_backups=(settings_default->getValue("no_file_backups", "false")=="true");
	settings.overwrite=false;
	settings.allow_overwrite=(settings_default->getValue("allow_overwrite", "true")=="true");
	settings.backupfolder=trim(settings_default->getValue(L"backupfolder", L"C:\\urbackup"));
	settings.backupfolder_uncompr=trim(settings_default->getValue(L"backupfolder_uncompr", settings.backupfolder));
	settings.client_overwrite=true;
	settings.autoshutdown=false;
	settings.startup_backup_delay=settings_default->getValue("startup_backup_delay", 0);
	settings.autoupdate_clients=(settings_default->getValue("autoupdate_clients", "true")=="true");
	settings.backup_window=settings_default->getValue("backup_window", "1-7/0-24");
	settings.max_active_clients=settings_default->getValue("max_active_clients", 100);
	settings.max_sim_backups=settings_default->getValue("max_sim_backups", 10);
	settings.exclude_files=settings_default->getValue(L"exclude_files", L"");
	settings.include_files=settings_default->getValue(L"include_files", L"");
	settings.default_dirs=settings_default->getValue(L"default_dirs", L"");
	settings.cleanup_window=settings_default->getValue("cleanup_window", "1-7/3-4");
	settings.allow_config_paths=(settings_default->getValue("allow_config_paths", "true")=="true");
	settings.allow_starting_file_backups=(settings_default->getValue("allow_starting_file_backups", "true")=="true");
	settings.allow_starting_image_backups=(settings_default->getValue("allow_starting_image_backups", "true")=="true");
	settings.allow_pause=(settings_default->getValue("allow_pause", "true")=="true");
	settings.allow_log_view=(settings_default->getValue("allow_log_view", "true")=="true");
	settings.image_letters=settings_default->getValue("image_letters", "C");
	settings.backup_database=(settings_default->getValue("backup_database", "true")=="true");
	settings.internet_server_port=(unsigned short)(atoi(settings_default->getValue("internet_server_port", "55415").c_str()));
	settings.client_set_settings=false;
	settings.internet_server_name=settings_default->getValue("internet_server_name", "");
	settings.internet_image_backups=(settings_default->getValue("internet_image_backups", "false")=="true");
	settings.internet_full_file_backups=(settings_default->getValue("internet_full_file_backups", "false")=="true");
	settings.internet_encrypt=(settings_default->getValue("internet_encrypt", "true")=="true");
	settings.internet_compress=(settings_default->getValue("internet_compress", "true")=="true");
	settings.internet_compression_level=atoi(settings_default->getValue("internet_compress", "6").c_str());
	settings.internet_speed=atoi(settings_default->getValue("internet_speed", "-1").c_str());
	settings.local_speed=atoi(settings_default->getValue("local_speed", "-1").c_str());
	settings.global_internet_speed=atoi(settings_default->getValue("global_internet_speed", "-1").c_str());
	settings.global_local_speed=atoi(settings_default->getValue("global_local_speed", "-1").c_str());
	settings.internet_mode_enabled=(settings_default->getValue("internet_mode_enabled", "false")=="true");
	settings.silent_update=(settings_default->getValue("silent_update", "false")=="true");
	settings.use_tmpfiles=(settings_default->getValue("use_tmpfiles", "false")=="true");
	settings.use_tmpfiles_images=(settings_default->getValue("use_tmpfiles_images", "false")=="true");
	settings.tmpdir=settings_default->getValue(L"tmpdir",L"");
	settings.local_full_file_transfer_mode=settings_default->getValue("local_full_file_transfer_mode", "hashed");
	settings.internet_full_file_transfer_mode=settings_default->getValue("internet_full_file_transfer_mode", "hashed");
	settings.local_incr_file_transfer_mode=settings_default->getValue("local_incr_file_transfer_mode", "hashed");
	settings.internet_incr_file_transfer_mode=settings_default->getValue("internet_incr_file_transfer_mode", "blockhash");
	settings.local_image_transfer_mode=settings_default->getValue("local_image_transfer_mode", "hashed");
	settings.internet_image_transfer_mode=settings_default->getValue("internet_image_transfer_mode", "hashed");
	settings.file_hash_collect_amount=static_cast<size_t>(settings_default->getValue("file_hash_collect_amount", 1000));
	settings.file_hash_collect_timeout=static_cast<size_t>(settings_default->getValue("file_hash_collect_timeout", 10000));
	settings.file_hash_collect_cachesize=static_cast<size_t>(settings_default->getValue("file_hash_collect_cachesize", 40960));
	settings.update_stats_cachesize=static_cast<size_t>(settings_default->getValue("update_stats_cachesize", 409600));
	settings.global_soft_fs_quota=settings_default->getValue("global_soft_fs_quota", "100%");
	settings.filescache_type=settings_default->getValue("filescache_type", "none");
	settings.filescache_size=watoi64(settings_default->getValue(L"filescache_size", L"68719476736")); //64GB
}

void ServerSettings::readSettingsClient(void)
{	
	std::string stmp=settings_client->getValue("internet_authkey", generateRandomAuthKey());
	if(!stmp.empty())
		settings.internet_authkey=stmp;

	stmp=settings_client->getValue("client_overwrite", "");
	if(!stmp.empty())
		settings.client_overwrite=(stmp=="true");

	if(!settings.client_overwrite)
		return;

	int tmp=settings_client->getValue("update_freq_incr", -1);
	if(tmp!=-1)
		settings.update_freq_incr=tmp;
	tmp=settings_client->getValue("update_freq_full", -1);
	if(tmp!=-1)
		settings.update_freq_full=tmp;
	tmp=settings_client->getValue("update_freq_image_incr", -1);
	if(tmp!=-1)
		settings.update_freq_image_incr=tmp;
	tmp=settings_client->getValue("update_freq_image_full", -1);
	if(tmp!=-1)
		settings.update_freq_image_full=tmp;
	tmp=settings_client->getValue("max_file_incr", -1);
	if(tmp!=-1)
		settings.max_file_incr=tmp;
	tmp=settings_client->getValue("min_file_incr", -1);
	if(tmp!=-1)
		settings.min_file_incr=tmp;
	tmp=settings_client->getValue("max_file_full", -1);
	if(tmp!=-1)
		settings.max_file_full=tmp;
	tmp=settings_client->getValue("min_file_full", -1);
	if(tmp!=-1)
		settings.min_file_full=tmp;
	tmp=settings_client->getValue("min_image_incr", -1);
	if(tmp!=-1)
		settings.min_image_incr=tmp;
	tmp=settings_client->getValue("max_image_incr", -1);
	if(tmp!=-1)
		settings.max_image_incr=tmp;
	tmp=settings_client->getValue("min_image_full", -1);
	if(tmp!=-1)
		settings.min_image_full=tmp;
	tmp=settings_client->getValue("max_image_full", -1);
	if(tmp!=-1)
		settings.max_image_full=tmp;
	tmp=settings_client->getValue("startup_backup_delay", -1);
	if(tmp!=-1)
		settings.startup_backup_delay=tmp;
	stmp=settings_client->getValue("backup_window", "");
	if(!stmp.empty())
		settings.backup_window=stmp;
	std::wstring swtmp=settings_client->getValue(L"computername", L"");
	if(!swtmp.empty())
		settings.computername=swtmp;
	if(settings_client->getValue(L"exclude_files", &swtmp))
		settings.exclude_files=swtmp;
	if(settings_client->getValue(L"include_files", &swtmp))
		settings.include_files=swtmp;
	swtmp=settings_client->getValue(L"default_dirs", L"");
	if(!swtmp.empty())
		settings.default_dirs=swtmp;
	stmp=settings_client->getValue("image_letters", "");
	if(!stmp.empty())
		settings.image_letters=stmp;
	stmp=settings_client->getValue("internet_speed", "");
	if(!stmp.empty())
		settings.internet_speed=atoi(stmp.c_str());
	stmp=settings_client->getValue("local_speed", "");
	if(!stmp.empty())
		settings.local_speed=atoi(stmp.c_str());

	readBoolClientSetting("client_set_settings", &settings.client_set_settings);
	readBoolClientSetting("internet_mode_enabled", &settings.internet_mode_enabled);
	readBoolClientSetting("internet_full_file_backups", &settings.internet_full_file_backups);
	readBoolClientSetting("internet_image_backups", &settings.internet_image_backups);
	readBoolClientSetting("internet_compress", &settings.internet_compress);
	readBoolClientSetting("internet_encrypt", &settings.internet_encrypt);
	readBoolClientSetting("silent_update", &settings.silent_update);	

	readBoolClientSetting("overwrite", &settings.overwrite);

	if(!settings.overwrite)
		return;

	readBoolClientSetting("allow_config_paths", &settings.allow_config_paths);
	readBoolClientSetting("allow_starting_file_backups", &settings.allow_starting_file_backups);
	readBoolClientSetting("allow_starting_image_backups", &settings.allow_starting_image_backups);
	readBoolClientSetting("allow_pause", &settings.allow_pause);
	readBoolClientSetting("allow_log_view", &settings.allow_log_view);
	readBoolClientSetting("allow_overwrite", &settings.allow_overwrite);
}

void ServerSettings::readBoolClientSetting(const std::string &name, bool *output)
{
	std::string value;
	if(settings_client->getValue(name, &value) && !value.empty())
	{
		if(value=="true")
			*output=true;
		else if(value=="false")
			*output=false;
	}
}

std::vector<STimeSpan> ServerSettings::getCleanupWindow(void)
{
	std::string window=getSettings()->cleanup_window;
	return getWindow(window);
}

std::vector<STimeSpan> ServerSettings::getBackupWindow(void)
{
	std::string window=getSettings()->backup_window;
	return getWindow(window);
}

std::vector<std::string> ServerSettings::getBackupVolumes(void)
{
	std::string vols=getSettings()->image_letters;
	std::vector<std::string> ret;
	Tokenize(vols, ret, ";,");
	for(size_t i=0;i<ret.size();++i)
	{
		ret[i]=trim(ret[i]);
	}
	return ret;
}

std::vector<STimeSpan> ServerSettings::getWindow(std::string window)
{
	std::vector<std::string> toks;
	Tokenize(window, toks, ";");

	std::vector<STimeSpan> ret;

	for(size_t i=0;i<toks.size();++i)
	{
		std::string c_tok=trim(toks[i]);
		std::string f_o=getuntil("/", c_tok);
		std::string b_o=getafter("/", c_tok);

		std::vector<std::string> dow_toks;
		Tokenize(f_o, dow_toks, ",");

		std::vector<std::string> time_toks;
		Tokenize(b_o, time_toks, ",");

		for(size_t l=0;l<dow_toks.size();++l)
		{
			f_o=trim(dow_toks[l]);
			if(f_o.find("-")!=std::string::npos)
			{
				std::string f1=trim(getuntil("-", f_o));
				std::string b2=trim(getafter("-", f_o));

				int start=parseDayOfWeek(f1);
				int stop=parseDayOfWeek(b2);

				if(stop<start) { int t=start; start=stop; stop=t; }

				if(start<1 || start>7 || stop<1 || stop>7)
				{
					ret.clear();
					return ret;
				}
				
				for(size_t o=0;o<time_toks.size();++o)
				{
					b_o=trim(time_toks[o]);

					STimeSpan ts=parseTime(b_o);
					if(ts.dayofweek==-1)
					{
						ret.clear();
						return ret;
					}				
			
					for(int j=start;j<=stop;++j)
					{				
						ts.dayofweek=j;
						ret.push_back(ts);
					}
				}
			}
			else
			{
				int j=parseDayOfWeek(f_o);
				if(j<1 || j>7)
				{
					ret.clear();
					return ret;
				}

				for(size_t o=0;o<time_toks.size();++o)
				{
					b_o=trim(time_toks[o]);
					STimeSpan ts=parseTime(b_o);
					if(ts.dayofweek==-1)
					{
						ret.clear();
						return ret;
					}
					ts.dayofweek=j;
					ret.push_back(ts);
				}
			}
		}
	}

	return ret;
}

float ServerSettings::parseTimeDet(std::string t)
{
	if(t.find(":")!=std::string::npos)
	{
		std::string h=getuntil(":", t);
		std::string m=getafter(":", t);

		return (float)atoi(h.c_str())+(float)atoi(m.c_str())*(1.f/60.f);
	}
	else
	{
		return (float)atoi(t.c_str());
	}
}

int ServerSettings::parseDayOfWeek(std::string dow)
{
	if(dow.size()==1 && str_isnumber(dow[0])==true)
	{
		int r=atoi(dow.c_str());
		if(r==0) r=7;
		return r;
	}
	else
	{
		dow=strlower(dow);
		if(dow=="mon" || dow=="mo" ) return 1;
		if(dow=="tu" || dow=="tue" || dow=="tues" || dow=="di" ) return 2;
		if(dow=="wed" || dow=="mi" ) return 3;
		if(dow=="th" || dow=="thu" || dow=="thur" || dow=="thurs" || dow=="do" ) return 4;
		if(dow=="fri" || dow=="fr" ) return 5;
		if(dow=="sat" || dow=="sa" ) return 6;
		if(dow=="sun" || dow=="so" ) return 7;
		return -1;
	}
}

STimeSpan ServerSettings::parseTime(std::string t)
{
	if(t.find("-")!=std::string::npos)
	{
		std::string f=trim(getuntil("-", t));
		std::string b=trim(getafter("-", t));

		return STimeSpan(parseTimeDet(f), parseTimeDet(b) );
	}
	else
	{
		return STimeSpan();
	}
}

std::string ServerSettings::generateRandomAuthKey(size_t len)
{
	std::string rchars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	std::string key;
	std::vector<unsigned int> rnd_n=Server->getSecureRandomNumbers(len);
	for(size_t j=0;j<len;++j)
		key+=rchars[rnd_n[j]%rchars.size()];
	return key;
}

std::string ServerSettings::generateRandomBinaryKey(void)
{
	std::string key;
	key.resize(32);
	Server->secureRandomFill((char*)key.data(), 32);
	return key;
}

#endif //CLIENT_ONLY
