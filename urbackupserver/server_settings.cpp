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

#include "../stringtools.h"
#include "../urbackupcommon/settingslist.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>
#include <assert.h>
#ifndef CLIENT_ONLY

#include "server_settings.h"
#include "../Interface/Server.h"

std::map<ServerSettings*, bool> ServerSettings::g_settings;
IMutex *ServerSettings::g_mutex=NULL;
std::map<int, SSettingsCacheItem> ServerSettings::g_settings_cache;

//#define CLEAR_SETTINGS_CACHE

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

void ServerSettings::clear_cache()
{
	IScopedLock lock(g_mutex);

	for(std::map<int, SSettingsCacheItem>::iterator it=g_settings_cache.begin();
		it!=g_settings_cache.end();)
	{
		if(it->second.refcount==0)
		{
			std::map<int, SSettingsCacheItem>::iterator delit=it++;
			delete delit->second.settings;
			g_settings_cache.erase(delit);
		}
		else
		{
			Server->Log("Refcount for settings for clientid \""+nconvert(it->second.settings->clientid)+"\" is not 0. Not deleting.", LL_WARNING);
			++it;
		}
	}
}

ServerSettings::ServerSettings(IDatabase *db, int pClientid)
	: local_settings(NULL), clientid(pClientid), settings_default(NULL),
		settings_client(NULL), db(db)
{
	IScopedLock lock(g_mutex);
		
	g_settings[this]=true;

	std::map<int, SSettingsCacheItem>::iterator iter=g_settings_cache.find(clientid);
	if(iter!=g_settings_cache.end())
	{
		++iter->second.refcount;
		settings_cache=&iter->second;
		do_update=settings_cache->needs_update;
		return;
	}
	else
	{
		SSettings* settings=new SSettings();
		SSettingsCacheItem cache_item = { settings, 1 , true};
		std::map<int, SSettingsCacheItem>::iterator iter = g_settings_cache.insert(std::make_pair(clientid, cache_item)).first;
		settings_cache=&iter->second;
		update(false);
		do_update=false;
	}	
}

void ServerSettings::createSettingsReaders()
{
	if(settings_default==NULL)
	{
		settings_default=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings_db.settings WHERE key=? AND clientid=0");
		if(clientid!=-1)
		{
			settings_client=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings_db.settings WHERE key=? AND clientid="+nconvert(clientid));
		}
		else
		{
			settings_client=NULL;
		}
	}
}

ServerSettings::~ServerSettings(void)
{
	if(settings_default!=NULL)
	{
		Server->destroy(settings_default);
	}
	if(settings_client!=NULL)
	{
		Server->destroy(settings_client);
	}

	{
		IScopedLock lock(g_mutex);

		std::map<ServerSettings*, bool>::iterator it=g_settings.find(this);
		assert(it!=g_settings.end());
		g_settings.erase(it);

		
		--settings_cache->refcount;
#ifdef CLEAR_SETTINGS_CACHE
		if(settings_cache->refcount==0)
		{
			std::map<int, SSettingsCacheItem>::iterator iter=g_settings_cache.find(clientid);
			assert(iter!=g_settings_cache.end());
			delete iter->second.settings;
			g_settings_cache.erase(iter);
		}
#endif
	}

	delete local_settings;
}

void ServerSettings::updateAll(void)
{
	IScopedLock lock(g_mutex);

	for(std::map<int, SSettingsCacheItem>::iterator it=g_settings_cache.begin();
		it!=g_settings_cache.end();)
	{
		if(it->second.refcount==0)
		{
			std::map<int, SSettingsCacheItem>::iterator delit=it++;
			delete delit->second.settings;
			g_settings_cache.erase(delit);
		}
		else
		{
			it->second.needs_update=true;
			++it;
		}
	}

	for(std::map<ServerSettings*, bool>::iterator it=g_settings.begin();
		it!=g_settings.end(); ++it)
	{
		it->first->doUpdate();
	}
}

void ServerSettings::update(bool force_update)
{
	createSettingsReaders();

	IScopedLock lock(g_mutex);

	if(settings_cache->needs_update || force_update)
	{
		readSettingsDefault();
		if(settings_client!=NULL)
		{
			readSettingsClient();
		}
		settings_cache->needs_update=false;
	}

	if(local_settings!=NULL)
	{
		delete local_settings;
		local_settings=new SSettings(*settings_cache->settings);
	}
}

void ServerSettings::doUpdate(void)
{
	do_update=true;
}

void ServerSettings::updateInternal(bool* was_updated)
{
	if(do_update)
	{
		if(was_updated!=NULL)
			*was_updated=true;

		do_update=false;
		update(false);
	}
	else
	{
		if(was_updated!=NULL)
			*was_updated=false;
	}
}

SSettings *ServerSettings::getSettings(bool *was_updated)
{
	updateInternal(was_updated);

	if(local_settings==NULL)
	{
		IScopedLock lock(g_mutex);
		local_settings=new SSettings(*settings_cache->settings);
	}

	return local_settings;
}

void ServerSettings::readSettingsDefault(void)
{
	SSettings* settings=settings_cache->settings;
	settings->clientid=clientid;
	settings->update_freq_incr=settings_default->getValue("update_freq_incr", 5*60*60);
	settings->update_freq_full=settings_default->getValue("update_freq_full", 30*24*60*60);
	settings->update_freq_image_incr=settings_default->getValue("update_freq_image_incr", 7*24*60*60);
	settings->update_freq_image_full=settings_default->getValue("update_freq_image_full", 60*24*60*60);
	settings->max_file_incr=settings_default->getValue("max_file_incr", 100);
	settings->min_file_incr=settings_default->getValue("min_file_incr", 40);
	settings->max_file_full=settings_default->getValue("max_file_full", 10);
	settings->min_file_full=settings_default->getValue("min_file_full", 2);
	settings->min_image_incr=settings_default->getValue("min_image_incr", 4);
	settings->max_image_incr=settings_default->getValue("max_image_incr", 30);
	settings->min_image_full=settings_default->getValue("min_image_full", 2);
	settings->max_image_full=settings_default->getValue("max_image_full", 5);
	settings->no_images=(settings_default->getValue("no_images", "false")=="true");
	settings->no_file_backups=(settings_default->getValue("no_file_backups", "false")=="true");
	settings->overwrite=false;
	settings->allow_overwrite=(settings_default->getValue("allow_overwrite", "true")=="true");
	settings->backupfolder=trim(settings_default->getValue(L"backupfolder", L"C:\\urbackup"));
	settings->backupfolder_uncompr=trim(settings_default->getValue(L"backupfolder_uncompr", settings->backupfolder));
	settings->client_overwrite=true;
	settings->autoshutdown=(settings_default->getValue("autoshutdown", "false")=="true");;
	settings->startup_backup_delay=settings_default->getValue("startup_backup_delay", 0);
	settings->download_client=(settings_default->getValue("download_client", "true")=="true");
	settings->autoupdate_clients=(settings_default->getValue("autoupdate_clients", "true")=="true");
	settings->backup_window_incr_file=settings_default->getValue("backup_window_incr_file", "1-7/0-24");
	settings->backup_window_full_file=settings_default->getValue("backup_window_full_file", "1-7/0-24");
	settings->backup_window_incr_image=settings_default->getValue("backup_window_incr_image", "1-7/0-24");
	settings->backup_window_full_image=settings_default->getValue("backup_window_full_image", "1-7/0-24");
	settings->max_active_clients=settings_default->getValue("max_active_clients", 100);
	settings->max_sim_backups=settings_default->getValue("max_sim_backups", 10);
	settings->exclude_files=settings_default->getValue(L"exclude_files", L"");
	settings->include_files=settings_default->getValue(L"include_files", L"");
	settings->default_dirs=settings_default->getValue(L"default_dirs", L"");
	settings->cleanup_window=settings_default->getValue("cleanup_window", "1-7/3-4");
	settings->allow_config_paths=(settings_default->getValue("allow_config_paths", "true")=="true");
	settings->allow_starting_full_file_backups=(settings_default->getValue("allow_starting_full_file_backups", "true")=="true");
	settings->allow_starting_incr_file_backups=(settings_default->getValue("allow_starting_incr_file_backups", "true")=="true");
	settings->allow_starting_full_image_backups=(settings_default->getValue("allow_starting_full_image_backups", "true")=="true");
	settings->allow_starting_incr_image_backups=(settings_default->getValue("allow_starting_incr_image_backups", "true")=="true");
	settings->allow_pause=(settings_default->getValue("allow_pause", "true")=="true");
	settings->allow_log_view=(settings_default->getValue("allow_log_view", "true")=="true");
	settings->allow_tray_exit=(settings_default->getValue("allow_tray_exit", "true")=="true");
	settings->image_letters=settings_default->getValue("image_letters", "C");
	settings->backup_database=(settings_default->getValue("backup_database", "true")=="true");
	settings->internet_server_port=(unsigned short)(atoi(settings_default->getValue("internet_server_port", "55415").c_str()));
	settings->client_set_settings=false;
	settings->internet_server=settings_default->getValue("internet_server", "");
	settings->internet_image_backups=(settings_default->getValue("internet_image_backups", "false")=="true");
	settings->internet_full_file_backups=(settings_default->getValue("internet_full_file_backups", "false")=="true");
	settings->internet_encrypt=(settings_default->getValue("internet_encrypt", "true")=="true");
	settings->internet_compress=(settings_default->getValue("internet_compress", "true")=="true");
	settings->internet_compression_level=atoi(settings_default->getValue("internet_compress", "6").c_str());
	settings->internet_speed=atoi(settings_default->getValue("internet_speed", "-1").c_str());
	settings->local_speed=atoi(settings_default->getValue("local_speed", "-1").c_str());
	settings->global_internet_speed=atoi(settings_default->getValue("global_internet_speed", "-1").c_str());
	settings->global_local_speed=atoi(settings_default->getValue("global_local_speed", "-1").c_str());
	settings->internet_mode_enabled=(settings_default->getValue("internet_mode_enabled", "false")=="true");
	settings->silent_update=(settings_default->getValue("silent_update", "false")=="true");
	settings->use_tmpfiles=(settings_default->getValue("use_tmpfiles", "false")=="true");
	settings->use_tmpfiles_images=(settings_default->getValue("use_tmpfiles_images", "false")=="true");
	settings->tmpdir=settings_default->getValue(L"tmpdir",L"");
	settings->local_full_file_transfer_mode=settings_default->getValue("local_full_file_transfer_mode", "hashed");
	settings->internet_full_file_transfer_mode=settings_default->getValue("internet_full_file_transfer_mode", "hashed");
	settings->local_incr_file_transfer_mode=settings_default->getValue("local_incr_file_transfer_mode", "hashed");
	settings->internet_incr_file_transfer_mode=settings_default->getValue("internet_incr_file_transfer_mode", "blockhash");
	settings->local_image_transfer_mode=settings_default->getValue("local_image_transfer_mode", "hashed");
	settings->internet_image_transfer_mode=settings_default->getValue("internet_image_transfer_mode", "hashed");
	settings->file_hash_collect_amount=static_cast<size_t>(settings_default->getValue("file_hash_collect_amount", 1000));
	settings->file_hash_collect_timeout=static_cast<size_t>(settings_default->getValue("file_hash_collect_timeout", 10000));
	settings->file_hash_collect_cachesize=static_cast<size_t>(settings_default->getValue("file_hash_collect_cachesize", 40960));
	settings->update_stats_cachesize=static_cast<size_t>(settings_default->getValue("update_stats_cachesize", 409600));
	settings->global_soft_fs_quota=settings_default->getValue("global_soft_fs_quota", "100%");
	settings->filescache_type=settings_default->getValue("filescache_type", "none");
	settings->filescache_size=watoi64(settings_default->getValue(L"filescache_size", L"68719476736")); //64GB
	settings->suspend_index_limit=settings_default->getValue("suspend_index_limit", 100000);
	settings->client_quota=settings_default->getValue("client_quota", "100%");
	settings->end_to_end_file_backup_verification=(settings_default->getValue("end_to_end_file_backup_verification", "false")=="true");
	if(is_big_endian())
	{
		settings->end_to_end_file_backup_verification=true;
	}
	settings->internet_calculate_filehashes_on_client=(settings_default->getValue("internet_calculate_filehashes_on_client", "true")=="true");
	settings->use_incremental_symlinks=(settings_default->getValue("use_incremental_symlinks", "true")=="true");
	settings->compress_images=(settings_default->getValue("compress_images", "true")=="true");
	settings->trust_client_hashes=(settings_default->getValue("trust_client_hashes", "true")=="true");
	settings->internet_connect_always=(settings_default->getValue("internet_connect_always", "false")=="true");
	settings->show_server_updates=(settings_default->getValue("show_server_updates", "true")=="true");
	settings->server_url=settings_default->getValue("server_url", "");
}

void ServerSettings::readSettingsClient(void)
{	
	SSettings* settings=settings_cache->settings;
	std::string stmp=settings_client->getValue("internet_authkey", std::string());
	if(!stmp.empty())
	{
		settings->internet_authkey=stmp;
	}
	else
	{
		settings->internet_authkey=generateRandomAuthKey();
	}

	settings->client_access_key = settings_client->getValue("client_access_key", std::string());

	stmp=settings_client->getValue("client_overwrite", "");
	if(!stmp.empty())
		settings->client_overwrite=(stmp=="true");

	if(!settings->client_overwrite)
		return;

	readBoolClientSetting("overwrite", &settings->overwrite);

	if(settings->overwrite)
	{
		readBoolClientSetting("allow_overwrite", &settings->allow_overwrite);
	}

	if(!settings->overwrite && !settings->allow_overwrite)
		return;

	int tmp=settings_client->getValue("update_freq_incr", -1);
	if(tmp!=-1)
		settings->update_freq_incr=tmp;
	tmp=settings_client->getValue("update_freq_full", -1);
	if(tmp!=-1)
		settings->update_freq_full=tmp;
	tmp=settings_client->getValue("update_freq_image_incr", -1);
	if(tmp!=-1)
		settings->update_freq_image_incr=tmp;
	tmp=settings_client->getValue("update_freq_image_full", -1);
	if(tmp!=-1)
		settings->update_freq_image_full=tmp;
	tmp=settings_client->getValue("max_file_incr", -1);
	if(tmp!=-1)
		settings->max_file_incr=tmp;
	tmp=settings_client->getValue("min_file_incr", -1);
	if(tmp!=-1)
		settings->min_file_incr=tmp;
	tmp=settings_client->getValue("max_file_full", -1);
	if(tmp!=-1)
		settings->max_file_full=tmp;
	tmp=settings_client->getValue("min_file_full", -1);
	if(tmp!=-1)
		settings->min_file_full=tmp;
	tmp=settings_client->getValue("min_image_incr", -1);
	if(tmp!=-1)
		settings->min_image_incr=tmp;
	tmp=settings_client->getValue("max_image_incr", -1);
	if(tmp!=-1)
		settings->max_image_incr=tmp;
	tmp=settings_client->getValue("min_image_full", -1);
	if(tmp!=-1)
		settings->min_image_full=tmp;
	tmp=settings_client->getValue("max_image_full", -1);
	if(tmp!=-1)
		settings->max_image_full=tmp;
	tmp=settings_client->getValue("startup_backup_delay", -1);
	if(tmp!=-1)
		settings->startup_backup_delay=tmp;	
	std::wstring swtmp=settings_client->getValue(L"computername", L"");
	if(!swtmp.empty())
		settings->computername=swtmp;
	if(settings_client->getValue(L"exclude_files", &swtmp))
		settings->exclude_files=swtmp;
	if(settings_client->getValue(L"include_files", &swtmp))
		settings->include_files=swtmp;
	swtmp=settings_client->getValue(L"default_dirs", L"");
	if(!swtmp.empty())
		settings->default_dirs=swtmp;
	stmp=settings_client->getValue("image_letters", "");
	if(!stmp.empty())
		settings->image_letters=stmp;
	stmp=settings_client->getValue("internet_speed", "");
	if(!stmp.empty())
		settings->internet_speed=atoi(stmp.c_str());
	stmp=settings_client->getValue("local_speed", "");
	if(!stmp.empty())
		settings->local_speed=atoi(stmp.c_str());

	readBoolClientSetting("client_set_settings", &settings->client_set_settings);
	readBoolClientSetting("internet_mode_enabled", &settings->internet_mode_enabled);
	readBoolClientSetting("internet_full_file_backups", &settings->internet_full_file_backups);
	readBoolClientSetting("internet_image_backups", &settings->internet_image_backups);
	readBoolClientSetting("internet_compress", &settings->internet_compress);
	readBoolClientSetting("internet_encrypt", &settings->internet_encrypt);
	readBoolClientSetting("internet_connect_always", &settings->internet_connect_always);

	if(!settings->overwrite)
		return;

	stmp=settings_client->getValue("backup_window_incr_file", "");
	if(!stmp.empty())
		settings->backup_window_incr_file=stmp;
	stmp=settings_client->getValue("backup_window_full_file", "");
	if(!stmp.empty())
		settings->backup_window_full_file=stmp;
	stmp=settings_client->getValue("backup_window_incr_image", "");
	if(!stmp.empty())
		settings->backup_window_incr_image=stmp;
	stmp=settings_client->getValue("backup_window_full_image", "");
	if(!stmp.empty())
		settings->backup_window_full_image=stmp;

	stmp=settings_client->getValue("client_quota", "");
	if(!stmp.empty())
		settings->client_quota=stmp;

	readStringClientSetting("local_full_file_transfer_mode", &settings->local_full_file_transfer_mode);
	readStringClientSetting("internet_full_file_transfer_mode", &settings->internet_full_file_transfer_mode);
	readStringClientSetting("internet_incr_file_transfer_mode", &settings->internet_incr_file_transfer_mode);
	readStringClientSetting("local_image_transfer_mode", &settings->local_image_transfer_mode);
	readStringClientSetting("internet_image_transfer_mode", &settings->internet_image_transfer_mode);
	readSizeClientSetting("file_hash_collect_amount", &settings->file_hash_collect_amount);
	readSizeClientSetting("file_hash_collect_timeout", &settings->file_hash_collect_timeout);
	readSizeClientSetting("file_hash_collect_cachesize", &settings->file_hash_collect_cachesize);

	readBoolClientSetting("end_to_end_file_backup_verification", &settings->end_to_end_file_backup_verification);
	if(is_big_endian())
	{
		settings->end_to_end_file_backup_verification=true;
	}
	readBoolClientSetting("internet_calculate_filehashes_on_client", &settings->internet_calculate_filehashes_on_client);	
	readBoolClientSetting("silent_update", &settings->silent_update);
	readBoolClientSetting("compress_images", &settings->compress_images);

	readBoolClientSetting("allow_config_paths", &settings->allow_config_paths);
	readBoolClientSetting("allow_starting_full_file_backups", &settings->allow_starting_full_file_backups);
	readBoolClientSetting("allow_starting_incr_file_backups", &settings->allow_starting_incr_file_backups);
	readBoolClientSetting("allow_starting_full_image_backups", &settings->allow_starting_full_image_backups);
	readBoolClientSetting("allow_starting_incr_image_backups", &settings->allow_starting_incr_image_backups);
	readBoolClientSetting("allow_pause", &settings->allow_pause);
	readBoolClientSetting("allow_log_view", &settings->allow_log_view);
	readBoolClientSetting("allow_tray_exit", &settings->allow_tray_exit);
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

void ServerSettings::readStringClientSetting(const std::string &name, std::string *output)
{
	std::string value;
	if(settings_client->getValue(name, &value) && !value.empty())
	{
		*output=value;
	}
}

void ServerSettings::readIntClientSetting(const std::string &name, int *output)
{
	std::string value;
	if(settings_client->getValue(name, &value) && !value.empty())
	{
		*output=atoi(value.c_str());
	}
}

void ServerSettings::readSizeClientSetting(const std::string &name, size_t *output)
{
	std::string value;
	if(settings_client->getValue(name, &value) && !value.empty())
	{
		*output=static_cast<size_t>(os_atoi64(value));
	}
}

std::vector<STimeSpan> ServerSettings::getCleanupWindow(void)
{
	std::string window=getSettings()->cleanup_window;
	return getWindow(window);
}

std::vector<STimeSpan> ServerSettings::getBackupWindowIncrFile(void)
{
	std::string window=getSettings()->backup_window_incr_file;
	return getWindow(window);
}

std::vector<STimeSpan> ServerSettings::getBackupWindowFullFile(void)
{
	std::string window=getSettings()->backup_window_full_file;
	return getWindow(window);
}

std::vector<STimeSpan> ServerSettings::getBackupWindowIncrImage(void)
{
	std::string window=getSettings()->backup_window_incr_image;
	return getWindow(window);
}

std::vector<STimeSpan> ServerSettings::getBackupWindowFullImage(void)
{
	std::string window=getSettings()->backup_window_full_image;
	return getWindow(window);
}

std::vector<std::string> ServerSettings::getBackupVolumes(const std::string& all_volumes)
{
	std::string vols=getSettings()->image_letters;
	if(vols=="ALL")
	{
		vols=all_volumes;
	}
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

int ServerSettings::getUpdateFreqImageIncr()
{
	updateInternal(NULL);
	IScopedLock lock(g_mutex);
	return settings_cache->settings->update_freq_image_incr;
}

int ServerSettings::getUpdateFreqFileIncr()
{
	updateInternal(NULL);
	IScopedLock lock(g_mutex);
	return settings_cache->settings->update_freq_incr;
}

int ServerSettings::getUpdateFreqImageFull()
{
	updateInternal(NULL);
	IScopedLock lock(g_mutex);
	return settings_cache->settings->update_freq_image_full;
}

int ServerSettings::getUpdateFreqFileFull()
{
	updateInternal(NULL);
	IScopedLock lock(g_mutex);
	return settings_cache->settings->update_freq_full;
}

#endif //CLIENT_ONLY

