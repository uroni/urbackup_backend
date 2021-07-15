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

#include "../stringtools.h"
#include "../urbackupcommon/settingslist.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#ifndef CLIENT_ONLY

#include "server_settings.h"
#include "../Interface/Server.h"
#include "server.h"

IMutex *ServerSettings::g_mutex=NULL;
std::map<int, SSettings*> ServerSettings::g_settings_cache;

#ifndef DEFAULT_BACKUP_FOLDER
#ifdef _WIN32
#define DEFAULT_BACKUP_FOLDER "C:\\urbackup"
#else
#define DEFAULT_BACKUP_FOLDER "/mnt/backups/urbackup"
#endif
#endif

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

	for(std::map<int, SSettings*>::iterator it=g_settings_cache.begin();
		it!=g_settings_cache.end();)
	{
		if(it->second->refcount==0)
		{
			std::map<int, SSettings*>::iterator it_curr = it;
			++it;
			delete it_curr->second;
			g_settings_cache.erase(it_curr);
		}
		else
		{
			Server->Log("Refcount for settings for clientid \""+convert(it->second->clientid)+"\" is not 0. Not deleting.", LL_WARNING);
			++it;
		}
	}
}

ServerSettings::ServerSettings(IDatabase *db, int pClientid)
	: local_settings(NULL), clientid(pClientid), db(db)
{
	IScopedLock lock(g_mutex);
		
	std::map<int, SSettings*>::iterator iter=g_settings_cache.find(clientid);
	if(iter!=g_settings_cache.end())
	{
		++iter->second->refcount;
		local_settings = iter->second;
	}
	else
	{
		lock.relock(NULL);

		readSettings();
		
		lock.relock(g_mutex);

		iter = g_settings_cache.find(clientid);

		if (iter != g_settings_cache.end())
		{
			delete local_settings;
			++iter->second->refcount;
			local_settings = iter->second;
		}
		else
		{
			g_settings_cache.insert(std::make_pair(clientid, local_settings));
		}
	}	
}

void ServerSettings::createSettingsReaders(IDatabase* db, int clientid, std::unique_ptr<ISettingsReader>& settings_default,
	std::unique_ptr<ISettingsReader>& settings_client, std::unique_ptr<ISettingsReader>& settings_global,
	int& settings_default_id)
{
	settings_client.reset(Server->createDBMemSettingsReader(db, "settings", "SELECT key,value FROM settings_db.settings WHERE clientid=" + convert(clientid)));

	if(clientid>0)
	{
		settings_default_id = settings_client->getValue("group_id", 0)*-1;

		if (settings_default_id != 0)
		{
			settings_global.reset(Server->createDBMemSettingsReader(db, "settings", "SELECT key,value FROM settings_db.settings WHERE clientid=0"));
		}
	}
	else
	{
		settings_default_id = 0;
	}

	settings_default.reset(Server->createDBMemSettingsReader(db, "settings", "SELECT key,value FROM settings_db.settings WHERE clientid="+convert(settings_default_id)));
}

ServerSettings::~ServerSettings(void)
{
	{
		IScopedLock lock(g_mutex);

		assert(local_settings->refcount > 0);
		--local_settings->refcount;

		if (local_settings->refcount == 0)
		{
#ifdef CLEAR_SETTINGS_CACHE
			if (!local_settings->needs_update)
			{
				std::map<int, SSettings*>::iterator iter = g_settings_cache.find(clientid);
				assert(iter != g_settings_cache.end());
				delete iter->second;
				g_settings_cache.erase(iter);
			}
#endif
			if (local_settings->needs_update)
			{
				std::map<int, SSettings*>::iterator iter = g_settings_cache.find(clientid);
				assert(iter != g_settings_cache.end());
				if (local_settings == iter->second)
				{
					g_settings_cache.erase(iter);
				}

				delete local_settings;
			}
		}
	}
}

void ServerSettings::updateAll(void)
{
	IScopedLock lock(g_mutex);

	for(std::map<int, SSettings*>::iterator it=g_settings_cache.begin();
		it!=g_settings_cache.end();)
	{
		if(it->second->refcount==0)
		{
			std::map<int, SSettings*>::iterator it_curr = it;
			++it;
			delete it_curr->second;
			g_settings_cache.erase(it_curr);
		}
		else
		{
			it->second->needs_update=true;
			++it;
		}
	}
}

void ServerSettings::updateClient(int clientid)
{
	IScopedLock lock(g_mutex);

	std::map<int, SSettings*>::iterator it = g_settings_cache.find(clientid);
	if(it!=g_settings_cache.end())
	{
		if (it->second->refcount == 0)
		{
			delete it->second;
			g_settings_cache.erase(it);
		}
		else
		{
			it->second->needs_update = true;
		}
	}
}

void ServerSettings::update(bool force_update)
{
	while(local_settings->needs_update || force_update)
	{
		force_update = false;

		IScopedLock lock(g_mutex);

		local_settings->needs_update = true;

		assert(local_settings->refcount > 0);
		--local_settings->refcount;

		if (local_settings->refcount == 0)
		{
			std::map<int, SSettings*>::iterator iter = g_settings_cache.find(clientid);
			if (iter != g_settings_cache.end()
				&& iter->second == local_settings)
			{
				delete iter->second;
				g_settings_cache.erase(iter);
			}
			else
			{
				if (iter != g_settings_cache.end()
					&& iter->second != local_settings)
				{
					delete local_settings;
					++iter->second->refcount;
					local_settings = iter->second;
					continue;
				}
				else
				{
					delete local_settings;
				}
			}
		}

		std::map<int, SSettings*>::iterator iter = g_settings_cache.find(clientid);
		if (iter != g_settings_cache.end()
			&& iter->second != local_settings)
		{
			++iter->second->refcount;
			local_settings = iter->second;
			continue;
		}

		SSettings* old_local_settings = local_settings;

		lock.relock(NULL);

		readSettings();

		lock.relock(g_mutex);

		iter = g_settings_cache.find(clientid);

		if (iter==g_settings_cache.end()
			|| iter->second == old_local_settings)
		{
			g_settings_cache[clientid] = local_settings;
		}
		else
		{
			delete local_settings;
			++iter->second->refcount;
			local_settings = iter->second;
		}
	}
}

void ServerSettings::updateInternal(bool* was_updated)
{
	if(local_settings->needs_update)
	{
		if(was_updated!=NULL)
			*was_updated=true;

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
	return local_settings;
}

void ServerSettings::readSettingsDefault(ISettingsReader* settings_default,
	ISettingsReader* settings_global, IQuery* q_get_client_setting)
{
	SSettings* settings = local_settings;

	if (settings_global != NULL)
	{
		settings->no_images = (settings_global->getValue("no_images", "false") == "true");
		settings->no_file_backups = (settings_global->getValue("no_file_backups", "false") == "true");
		settings->backupfolder = trim(settings_global->getValue("backupfolder", DEFAULT_BACKUP_FOLDER));
		settings->backupfolder_uncompr = trim(settings_global->getValue("backupfolder_uncompr", settings->backupfolder));
		settings->autoshutdown = (settings_global->getValue("autoshutdown", "false") == "true");;
		settings->download_client = (settings_global->getValue("download_client", "true") == "true");
		settings->autoupdate_clients = (settings_global->getValue("autoupdate_clients", "true") == "true");
		settings->max_active_clients = settings_global->getValue("max_active_clients", 10000);
		settings->max_sim_backups = settings_global->getValue("max_sim_backups", 100);
		settings->cleanup_window = settings_global->getValue("cleanup_window", "1-7/3-4");
		settings->backup_database = (settings_global->getValue("backup_database", "true") == "true");
		settings->internet_server_port = (unsigned short)(atoi(settings_global->getValue("internet_server_port", "55415").c_str()));
		settings->internet_server_proxy = settings_global->getValue("internet_server_proxy", "");
		settings->internet_server = settings_global->getValue("internet_server", "");
		settings->global_internet_speed = settings_global->getValue("global_internet_speed", "-1");
		settings->global_local_speed = settings_global->getValue("global_local_speed", "-1");
		settings->use_tmpfiles = (settings_global->getValue("use_tmpfiles", "false") == "true");
		settings->use_tmpfiles_images = (settings_global->getValue("use_tmpfiles_images", "false") == "true");
		settings->tmpdir = settings_global->getValue("tmpdir", "");
		settings->update_stats_cachesize = static_cast<size_t>(settings_global->getValue("update_stats_cachesize", 200 * 1024));
		settings->global_soft_fs_quota = settings_global->getValue("global_soft_fs_quota", "95%");
		settings->use_incremental_symlinks = (settings_global->getValue("use_incremental_symlinks", "true") == "true");
		settings->show_server_updates = (settings_global->getValue("show_server_updates", "true") == "true");
		settings->server_url = trim(settings_global->getValue("server_url", ""));
	}

	settings->image_file_format = image_file_format_default;
	readStringClientSetting(q_get_client_setting, "image_file_format", std::string(), &settings->image_file_format, false);

	settings->update_freq_incr = convert(5 * 60 * 60);
	readStringClientSetting(q_get_client_setting, "update_freq_incr", std::string(), &settings->update_freq_incr, false);

	settings->update_freq_full = convert(30 * 24 * 60 * 60);
	readStringClientSetting(q_get_client_setting, "update_freq_full", std::string(), &settings->update_freq_full, false);
	
	settings->update_freq_image_incr = convert(7 * 24 * 60 * 60);
	readStringClientSetting(q_get_client_setting, "update_freq_image_incr", std::string(), &settings->update_freq_image_incr, false);
	
	if(getImageFileFormatInt(settings->image_file_format)==image_file_format_cowraw)
	{
		settings->update_freq_image_full=convert( 60*24*60*60);
	}
	else
	{
		settings->update_freq_image_full=convert( -60*24*60*60);
	}	
	readStringClientSetting(q_get_client_setting, "update_freq_image_full", std::string(), &settings->update_freq_image_full, false);

	settings->max_file_incr=100;
	readIntClientSetting(q_get_client_setting, "max_file_incr", &settings->max_file_incr, false);

	settings->min_file_incr=40;
	readIntClientSetting(q_get_client_setting, "min_file_incr", &settings->min_file_incr), false;

	settings->max_file_full=10;
	readIntClientSetting(q_get_client_setting, "max_file_full", &settings->max_file_full, false);

	settings->min_file_full=2;
	readIntClientSetting(q_get_client_setting, "min_file_full", &settings->min_file_full, false);

	settings->min_image_incr = 4;
	readIntClientSetting(q_get_client_setting, "min_image_incr", &settings->min_image_incr, false);

	settings->max_image_incr=30;
	readIntClientSetting(q_get_client_setting, "max_image_incr", &settings->max_image_incr, false);

	settings->min_image_full=2;
	readIntClientSetting(q_get_client_setting, "min_image_full", &settings->min_image_full, false);

	settings->max_image_full=5;
	readIntClientSetting(q_get_client_setting, "max_image_full", &settings->max_image_full, false);

	
	settings->allow_overwrite = true;
	readBoolClientSetting(q_get_client_setting, "allow_overwrite", &settings->allow_overwrite, false);
	
	settings->startup_backup_delay = 0;
	readIntClientSetting(q_get_client_setting, "startup_backup_delay", &settings->startup_backup_delay, false);
	
	settings->backup_window_incr_file= "1-7/0-24";
	readStringClientSetting(q_get_client_setting, "backup_window_incr_file", std::string(), &settings->backup_window_incr_file, false);

	settings->backup_window_full_file="1-7/0-24";
	readStringClientSetting(q_get_client_setting, "backup_window_full_file", std::string(), &settings->backup_window_full_file, false);

	settings->backup_window_incr_image="1-7/0-24";
	readStringClientSetting(q_get_client_setting, "backup_window_incr_image", std::string(), &settings->backup_window_incr_image, false);

	settings->backup_window_full_image="1-7/0-24";
	readStringClientSetting(q_get_client_setting, "backup_window_full_image", std::string(), &settings->backup_window_full_image, false);

	readStringClientSetting(q_get_client_setting, "exclude_files", ";", &settings->exclude_files, false);
	readStringClientSetting(q_get_client_setting, "include_files", ";", &settings->include_files, false);
	readStringClientSetting(q_get_client_setting, "default_dirs", ";", &settings->default_dirs, false);

	settings->backup_dirs_optional = false;
	readBoolClientSetting(q_get_client_setting, "backup_dirs_optional", &settings->backup_dirs_optional, false);
	
	settings->allow_config_paths = true;
	readBoolClientSetting(q_get_client_setting, "allow_config_paths", &settings->allow_config_paths, false);

	settings->allow_starting_full_file_backups = true;
	readBoolClientSetting(q_get_client_setting, "allow_starting_full_file_backups", &settings->allow_starting_full_file_backups, false);

	settings->allow_starting_incr_file_backups = true;
	readBoolClientSetting(q_get_client_setting, "allow_starting_incr_file_backups", &settings->allow_starting_incr_file_backups, false);

	settings->allow_starting_full_image_backups = true;
	readBoolClientSetting(q_get_client_setting, "allow_starting_full_image_backups", &settings->allow_starting_full_image_backups, false);

	settings->allow_starting_incr_image_backups = true;
	readBoolClientSetting(q_get_client_setting, "allow_starting_incr_image_backups", &settings->allow_starting_incr_image_backups, false);

	settings->allow_pause = true;
	readBoolClientSetting(q_get_client_setting, "allow_pause", &settings->allow_pause, false);

	settings->allow_log_view = true;
	readBoolClientSetting(q_get_client_setting, "allow_log_view", &settings->allow_log_view, false);

	settings->allow_tray_exit = true;
	readBoolClientSetting(q_get_client_setting, "allow_tray_exit", &settings->allow_tray_exit, false);

	settings->image_letters = "C";
	readStringClientSetting(q_get_client_setting, "image_letters", ";", &settings->image_letters, false);
		
	settings->client_set_settings=false;
	
	settings->internet_image_backups = false;
	readBoolClientSetting(q_get_client_setting, "internet_image_backups", &settings->internet_image_backups, false);

	settings->internet_full_file_backups = false;
	readBoolClientSetting(q_get_client_setting, "internet_full_file_backups", &settings->internet_full_file_backups, false);

	settings->internet_encrypt = true;
	readBoolClientSetting(q_get_client_setting, "internet_encrypt", &settings->internet_encrypt, false);

	settings->internet_compress = true;
	readBoolClientSetting(q_get_client_setting, "internet_compress", &settings->internet_compress, false);

	settings->internet_compression_level = 6;
	readIntClientSetting(q_get_client_setting, "internet_compression_level", &settings->internet_compression_level, false);

	settings->internet_speed = "-1";
	readStringClientSetting(q_get_client_setting, "internet_speed", std::string(), &settings->internet_speed, false);

	settings->local_speed = "-1";
	readStringClientSetting(q_get_client_setting, "local_speed", std::string(), &settings->local_speed, false);
	
	settings->internet_mode_enabled = true;
	readBoolClientSetting(q_get_client_setting, "internet_mode_enabled", &settings->internet_mode_enabled, false);

	settings->silent_update = true;
	readBoolClientSetting(q_get_client_setting, "silent_update", &settings->silent_update, false);
	
	settings->local_full_file_transfer_mode="hashed";
	readStringClientSetting(q_get_client_setting, "local_full_file_transfer_mode", std::string(), &settings->local_full_file_transfer_mode, false);

	settings->internet_full_file_transfer_mode="raw";
	readStringClientSetting(q_get_client_setting, "internet_full_file_transfer_mode", std::string(), &settings->internet_full_file_transfer_mode, false);

	settings->local_incr_file_transfer_mode="hashed";
	readStringClientSetting(q_get_client_setting, "local_incr_file_transfer_mode", std::string(), &settings->local_incr_file_transfer_mode, false);

	settings->internet_incr_file_transfer_mode="blockhash";
	readStringClientSetting(q_get_client_setting, "internet_incr_file_transfer_mode", std::string(), &settings->internet_incr_file_transfer_mode, false);

	settings->local_image_transfer_mode="hashed";
	readStringClientSetting(q_get_client_setting, "local_image_transfer_mode", std::string(), &settings->local_image_transfer_mode, false);

	settings->internet_image_transfer_mode="raw";
	readStringClientSetting(q_get_client_setting, "internet_image_transfer_mode", std::string(), &settings->internet_image_transfer_mode, false);
	
	readStringClientSetting(q_get_client_setting, "client_quota", std::string(), &settings->client_quota, false);
	settings->end_to_end_file_backup_verification = false;

	readBoolClientSetting(q_get_client_setting, "end_to_end_file_backup_verification", &settings->end_to_end_file_backup_verification, false);
	settings->internet_calculate_filehashes_on_client = true;

	readBoolClientSetting(q_get_client_setting, "internet_calculate_filehashes_on_client", &settings->internet_calculate_filehashes_on_client, false);
	settings->internet_parallel_file_hashing = false;

	readBoolClientSetting(q_get_client_setting, "internet_parallel_file_hashing", &settings->internet_parallel_file_hashing, false);
	
	settings->internet_connect_always = false;
	readBoolClientSetting(q_get_client_setting, "internet_connect_always", &settings->internet_connect_always, false);
	
	settings->verify_using_client_hashes = false;
	readBoolClientSetting(q_get_client_setting, "verify_using_client_hashes", &settings->verify_using_client_hashes, false);

	settings->internet_readd_file_entries = true;
	readBoolClientSetting(q_get_client_setting, "internet_readd_file_entries", &settings->internet_readd_file_entries, false);

	settings->max_running_jobs_per_client = 1;
	readIntClientSetting(q_get_client_setting, "max_running_jobs_per_client", &settings->max_running_jobs_per_client, false);

	settings->create_linked_user_views = false;
	readBoolClientSetting(q_get_client_setting, "create_linked_user_views", &settings->create_linked_user_views, false);

	settings->background_backups = true;
	readBoolClientSetting(q_get_client_setting, "background_backups", &settings->background_backups, false);

	settings->local_incr_image_style=incr_image_style_to_full;
	readStringClientSetting(q_get_client_setting, "local_incr_image_style", std::string(), &settings->local_incr_image_style, false);

	settings->local_full_image_style= full_image_style_full;
	readStringClientSetting(q_get_client_setting, "local_full_image_style", std::string(), &settings->local_full_image_style, false);

	settings->internet_incr_image_style=incr_image_style_to_last;
	readStringClientSetting(q_get_client_setting, "internet_incr_image_style", std::string(), &settings->internet_incr_image_style, false);

	settings->internet_full_image_style=full_image_style_synthetic;
	readStringClientSetting(q_get_client_setting, "internet_full_image_style", std::string(), &settings->internet_full_image_style, false);

	settings->backup_ok_mod_file = settings_default->getValue("backup_ok_mod_file", 3.f);
	settings->backup_ok_mod_image = settings_default->getValue("backup_ok_mod_image", 3.f);

	settings->cbt_volumes = "ALL";
	readStringClientSetting(q_get_client_setting, "cbt_volumes", std::string(";"), &settings->cbt_volumes, false);

	settings->cbt_crash_persistent_volumes = "-";
	readStringClientSetting(q_get_client_setting, "cbt_crash_persistent_volumes", std::string(";"), &settings->cbt_crash_persistent_volumes, false);

	settings->ignore_disk_errors = false;
	readBoolClientSetting(q_get_client_setting, "ignore_disk_errors", &settings->ignore_disk_errors, false);

	settings->vss_select_components = "default=1";
	readStringClientSetting(q_get_client_setting, "vss_select_components", std::string("&"), &settings->vss_select_components, false);

	settings->allow_file_restore = true;
	readBoolClientSetting(q_get_client_setting, "allow_file_restore", &settings->allow_file_restore, false);

	settings->allow_component_restore = true;
	readBoolClientSetting(q_get_client_setting, "allow_component_restore", &settings->allow_component_restore, false);

	settings->allow_component_config = true;
	readBoolClientSetting(q_get_client_setting, "allow_component_config", &settings->allow_component_config, false);

	readStringClientSetting(q_get_client_setting, "image_snapshot_groups", std::string(), &settings->image_snapshot_groups, false);
	readStringClientSetting(q_get_client_setting, "file_snapshot_groups", std::string(), &settings->file_snapshot_groups, false);

	settings->internet_file_dataplan_limit = 5LL*1000*1024*1024;
	readInt64ClientSetting(q_get_client_setting, "internet_file_dataplan_limit", &settings->internet_file_dataplan_limit, false);

	settings->internet_image_dataplan_limit = 20LL * 1000* 1024 * 1024;
	readInt64ClientSetting(q_get_client_setting, "internet_image_dataplan_limit", &settings->internet_image_dataplan_limit, false);

	settings->alert_script = 1;
	readIntClientSetting(q_get_client_setting, "alert_script", &settings->alert_script, false);
	readStringClientSetting(q_get_client_setting, "alert_params", std::string(), &settings->alert_params, false);

	readStringClientSetting(q_get_client_setting, "archive", std::string("&"), &settings->archive, false);

	readStringClientSetting(q_get_client_setting, "client_settings_tray_access_pw", std::string(), &settings->client_settings_tray_access_pw, false);

	settings->local_encrypt = true;
	readBoolClientSetting(q_get_client_setting, "local_encrypt", &settings->local_encrypt, false);

	settings->local_compress = true;
	readBoolClientSetting(q_get_client_setting, "local_compress", &settings->local_compress, false);

	settings->download_threads = 1;
	readIntClientSetting(q_get_client_setting, "download_threads", &settings->download_threads, false);
	settings->hash_threads = 1;
	readIntClientSetting(q_get_client_setting, "hash_threads", &settings->hash_threads, false);
	settings->client_hash_threads = 1;
	readIntClientSetting(q_get_client_setting, "client_hash_threads", &settings->client_hash_threads, false);
	settings->image_compress_threads = 0;
	readIntClientSetting(q_get_client_setting, "image_compress_threads", &settings->image_compress_threads, false);

	readStringClientSetting(q_get_client_setting, "ransomware_canary_paths", std::string(";"), &settings->ransomware_canary_paths, false);

	readStringClientSetting(q_get_client_setting, "backup_dest_url", std::string(), &settings->backup_dest_url, false);
	readStringClientSetting(q_get_client_setting, "backup_dest_params", "&", &settings->backup_dest_params, false);
	readStringClientSetting(q_get_client_setting, "backup_dest_secret_params", "&", &settings->backup_dest_secret_params, false);

	readStringClientSetting(q_get_client_setting, "backup_unlocked_window", std::string(), &settings->backup_unlocked_window, false);
	settings->pause_if_windows_unlocked = false;
	readBoolClientSetting(q_get_client_setting, "pause_if_windows_unlocked", &settings->pause_if_windows_unlocked, false);
}

void ServerSettings::readSettingsClient(ISettingsReader* settings_client, IQuery* q_get_client_setting)
{	
	SSettings* settings = local_settings;
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

	readStringClientSetting(q_get_client_setting, "update_freq_incr", std::string(), &settings->update_freq_incr);
	readStringClientSetting(q_get_client_setting, "update_freq_full", std::string(), &settings->update_freq_full);
	readStringClientSetting(q_get_client_setting, "update_freq_image_incr", std::string(), &settings->update_freq_image_incr);
	readStringClientSetting(q_get_client_setting, "update_freq_image_full", std::string(), &settings->update_freq_image_full);

	readIntClientSetting(q_get_client_setting, "max_file_incr", &settings->max_file_incr);
	readIntClientSetting(q_get_client_setting, "min_file_incr", &settings->min_file_incr);
	readIntClientSetting(q_get_client_setting, "max_file_full", &settings->max_file_full);
	readIntClientSetting(q_get_client_setting, "min_file_full", &settings->min_file_full);
	readIntClientSetting(q_get_client_setting, "min_image_incr", &settings->min_image_incr);
	readIntClientSetting(q_get_client_setting, "max_image_incr", &settings->max_image_incr);
	readIntClientSetting(q_get_client_setting, "min_image_full", &settings->min_image_full);
	readIntClientSetting(q_get_client_setting, "max_image_full", &settings->max_image_full);

	readIntClientSetting(q_get_client_setting, "startup_backup_delay", &settings->startup_backup_delay);
	readStringClientSetting(q_get_client_setting, "computername", std::string(), &settings->computername);
	readStringClientSetting(q_get_client_setting, "virtual_clients", ";", &settings->virtual_clients);
	readStringClientSetting(q_get_client_setting, "exclude_files", ";", &settings->exclude_files);
	readStringClientSetting(q_get_client_setting, "include_files", ";", &settings->include_files);
	readStringClientSetting(q_get_client_setting, "default_dirs", ";", &settings->default_dirs);
	readStringClientSetting(q_get_client_setting, "image_letters", ";", &settings->image_letters);
	readStringClientSetting(q_get_client_setting, "internet_speed", "", &settings->internet_speed);
	readStringClientSetting(q_get_client_setting, "local_speed", "", &settings->local_speed);

	readBoolClientSetting(q_get_client_setting, "backup_dirs_optional", &settings->backup_dirs_optional);
	readBoolClientSetting(q_get_client_setting, "internet_mode_enabled", &settings->internet_mode_enabled);
	readBoolClientSetting(q_get_client_setting, "internet_full_file_backups", &settings->internet_full_file_backups);
	readBoolClientSetting(q_get_client_setting, "internet_image_backups", &settings->internet_image_backups);
	readBoolClientSetting(q_get_client_setting, "internet_compress", &settings->internet_compress);
	readBoolClientSetting(q_get_client_setting, "internet_encrypt", &settings->internet_encrypt);
	readBoolClientSetting(q_get_client_setting, "internet_connect_always", &settings->internet_connect_always);
	readBoolClientSetting(q_get_client_setting, "local_encrypt", &settings->local_encrypt);
	readBoolClientSetting(q_get_client_setting, "local_compress", &settings->local_compress);

	readStringClientSetting(q_get_client_setting, "vss_select_components", "&", &settings->vss_select_components);

	if (settings->image_snapshot_groups.empty())
	{
		readStringClientSetting(q_get_client_setting, "image_snapshot_groups_def", std::string(), &settings->image_snapshot_groups);
	}

	readStringClientSetting(q_get_client_setting, "virtual_clients_add", std::string(), &settings->virtual_clients_add);

	//Following settings are not configurable by the client

	readStringClientSetting(q_get_client_setting, "backup_window_incr_file", std::string(), &settings->backup_window_incr_file, false);
	readStringClientSetting(q_get_client_setting, "backup_window_full_file", std::string(), &settings->backup_window_full_file, false);
	readStringClientSetting(q_get_client_setting, "backup_window_incr_image", std::string(), &settings->backup_window_incr_image, false);
	readStringClientSetting(q_get_client_setting, "backup_window_full_image", std::string(), &settings->backup_window_full_image, false);
	readStringClientSetting(q_get_client_setting, "local_incr_file_transfer_mode", std::string(), &settings->local_incr_file_transfer_mode, false);
	readStringClientSetting(q_get_client_setting, "local_full_file_transfer_mode", std::string(), &settings->local_full_file_transfer_mode, false);
	readStringClientSetting(q_get_client_setting, "internet_full_file_transfer_mode", std::string(), &settings->internet_full_file_transfer_mode, false);
	readStringClientSetting(q_get_client_setting, "internet_incr_file_transfer_mode", std::string(), &settings->internet_incr_file_transfer_mode, false);
	readStringClientSetting(q_get_client_setting, "local_image_transfer_mode", std::string(), &settings->local_image_transfer_mode, false);
	readStringClientSetting(q_get_client_setting, "internet_image_transfer_mode", std::string(), &settings->internet_image_transfer_mode, false);
	
	readBoolClientSetting(q_get_client_setting, "end_to_end_file_backup_verification", &settings->end_to_end_file_backup_verification, false);
	readBoolClientSetting(q_get_client_setting, "internet_calculate_filehashes_on_client", &settings->internet_calculate_filehashes_on_client, false);
	readBoolClientSetting(q_get_client_setting, "internet_parallel_file_hashing", &settings->internet_parallel_file_hashing, false);
	readBoolClientSetting(q_get_client_setting, "silent_update", &settings->silent_update, false);

	readBoolClientSetting(q_get_client_setting, "allow_overwrite", &settings->allow_overwrite, false);
	readBoolClientSetting(q_get_client_setting, "allow_config_paths", &settings->allow_config_paths, false);
	readBoolClientSetting(q_get_client_setting, "allow_starting_full_file_backups", &settings->allow_starting_full_file_backups, false);
	readBoolClientSetting(q_get_client_setting, "allow_starting_incr_file_backups", &settings->allow_starting_incr_file_backups, false);
	readBoolClientSetting(q_get_client_setting, "allow_starting_full_image_backups", &settings->allow_starting_full_image_backups, false);
	readBoolClientSetting(q_get_client_setting, "allow_starting_incr_image_backups", &settings->allow_starting_incr_image_backups, false);
	readBoolClientSetting(q_get_client_setting, "allow_pause", &settings->allow_pause, false);
	readBoolClientSetting(q_get_client_setting, "allow_log_view", &settings->allow_log_view, false);
	readBoolClientSetting(q_get_client_setting, "allow_tray_exit", &settings->allow_tray_exit, false);
	readBoolClientSetting(q_get_client_setting, "verify_using_client_hashes", &settings->verify_using_client_hashes, false);
	readBoolClientSetting(q_get_client_setting, "internet_readd_file_entries", &settings->internet_readd_file_entries, false);
	readBoolClientSetting(q_get_client_setting, "background_backups", &settings->background_backups, false);
	readIntClientSetting(q_get_client_setting, "max_running_jobs_per_client", &settings->max_running_jobs_per_client, false);
	readBoolClientSetting(q_get_client_setting, "create_linked_user_views", &settings->create_linked_user_views, false);

	readStringClientSetting(q_get_client_setting, "local_incr_image_style", std::string(), &settings->local_incr_image_style, false);
	readStringClientSetting(q_get_client_setting, "local_full_image_style", std::string(), &settings->local_full_image_style, false);
	readStringClientSetting(q_get_client_setting, "internet_incr_image_style", std::string(), &settings->internet_incr_image_style, false);
	readStringClientSetting(q_get_client_setting, "internet_full_image_style", std::string(), &settings->internet_full_image_style, false);

	readStringClientSetting(q_get_client_setting, "cbt_volumes", std::string(), &settings->cbt_volumes, false);
	readStringClientSetting(q_get_client_setting, "cbt_crash_persistent_volumes", std::string(), &settings->cbt_crash_persistent_volumes, false);

	readBoolClientSetting(q_get_client_setting, "ignore_disk_errors", &settings->ignore_disk_errors);

	readBoolClientSetting(q_get_client_setting, "allow_file_restore", &settings->allow_file_restore, false);
	readBoolClientSetting(q_get_client_setting, "allow_component_config", &settings->allow_component_config, false);
	readBoolClientSetting(q_get_client_setting, "allow_component_restore", &settings->allow_component_restore, false);

	readStringClientSetting(q_get_client_setting, "image_snapshot_groups", std::string(), &settings->image_snapshot_groups, false);
	readStringClientSetting(q_get_client_setting, "file_snapshot_groups", std::string(), &settings->file_snapshot_groups, false);

	readInt64ClientSetting(q_get_client_setting, "internet_file_dataplan_limit", &settings->internet_file_dataplan_limit, false);
	readInt64ClientSetting(q_get_client_setting, "internet_image_dataplan_limit", &settings->internet_image_dataplan_limit, false);

	readIntClientSetting(q_get_client_setting, "alert_script", &settings->alert_script, false);
	readStringClientSetting(q_get_client_setting, "alert_params", std::string(), &settings->alert_params, false);
	readStringClientSetting(q_get_client_setting, "archive", std::string("&"), &settings->archive, false);

	readStringClientSetting(q_get_client_setting, "client_settings_tray_access_pw", std::string(), &settings->client_settings_tray_access_pw, false);

	readIntClientSetting(q_get_client_setting, "download_threads", &settings->download_threads, false);
	readIntClientSetting(q_get_client_setting, "hash_threads", &settings->hash_threads, false);
	readIntClientSetting(q_get_client_setting, "client_hash_threads", &settings->client_hash_threads, false);
	readIntClientSetting(q_get_client_setting, "image_compress_threads", &settings->image_compress_threads, false);

	readStringClientSetting(q_get_client_setting, "ransomware_canary_paths", ";", &settings->ransomware_canary_paths, false);

	readStringClientSetting(q_get_client_setting, "backup_dest_url", std::string(), &settings->backup_dest_url);
	readStringClientSetting(q_get_client_setting, "backup_dest_params", "&", &settings->backup_dest_params);
	readStringClientSetting(q_get_client_setting, "backup_dest_secret_params", "&", &settings->backup_dest_secret_params);

	readStringClientSetting(q_get_client_setting, "backup_unlocked_window", std::string(), &settings->backup_unlocked_window, false);
	readBoolClientSetting(q_get_client_setting, "pause_if_windows_unlocked", &settings->pause_if_windows_unlocked, false);
}

void ServerSettings::readStringClientSetting(IQuery * q_get_client_setting, int clientid, const std::string & name, const std::string & merge_sep, std::string * output, bool allow_client_value)
{
	q_get_client_setting->Bind(clientid);
	q_get_client_setting->Bind(name);

	db_results res = q_get_client_setting->Read();

	q_get_client_setting->Reset();

	if (!res.empty())
	{
		int use = watoi(res[0]["use"]);

		if (use == c_use_value
			|| clientid==0)
		{
			*output = res[0]["value"];
		}
		else if (use == c_use_value_client
			&& allow_client_value)
		{
			*output = res[0]["value_client"];
		}
		else if(!merge_sep.empty()
			&& use!=c_use_group)
		{
			std::vector<std::string> all_toks;

			if (use & c_use_group)
			{
				std::vector<std::string> toks;
				Tokenize(*output, toks, merge_sep);
				all_toks.insert(all_toks.end(), toks.begin(), toks.end());
			}
			if (use & c_use_value)
			{
				std::vector<std::string> toks;
				Tokenize(res[0]["value"], toks, merge_sep);
				all_toks.insert(all_toks.end(), toks.begin(), toks.end());
			}
			if (use & c_use_value_client)
			{
				std::vector<std::string> toks;
				Tokenize(res[0]["value_client"], toks, merge_sep);
				all_toks.insert(all_toks.end(), toks.begin(), toks.end());
			}
			
			output->clear();
			for (size_t i = 0; i < all_toks.size(); ++i)
			{
				if (!output->empty())
					*output += merge_sep;
				*output += all_toks[i];
			}
		}
		else if (!allow_client_value
			&& use != c_use_group)
		{
			*output = res[0]["value"];
		}
	}
}

void ServerSettings::readStringClientSetting(IQuery* q_get_client_setting, const std::string & name, const std::string & merge_sep, std::string * output, bool allow_client_value)
{
	readStringClientSetting(q_get_client_setting, clientid, name, merge_sep, output, allow_client_value);
}

std::string ServerSettings::readValClientSetting(IQuery * q_get_client_setting, const std::string & name, bool allow_client_value)
{
	q_get_client_setting->Bind(clientid);
	q_get_client_setting->Bind(name);

	db_results res = q_get_client_setting->Read();

	q_get_client_setting->Reset();

	if (!res.empty())
	{
		int use = watoi(res[0]["use"]);

		std::string val;
		if (use == c_use_value
			|| clientid==0)
		{
			val = res[0]["value"];
		}
		else if (use == c_use_value_client
			&& allow_client_value)
		{
			val = res[0]["value_client"];
		}
		else if (!allow_client_value
			&& use != c_use_group)
		{
			val = res[0]["value"];
		}

		return val;
	}

	return std::string();
}

void ServerSettings::readBoolClientSetting(IQuery * q_get_client_setting, const std::string & name, bool * output, bool allow_client_value)
{
	std::string val = readValClientSetting(q_get_client_setting, name, allow_client_value);

	if (val == "true"
		|| val == "1")
		*output = true;
	else if (val == "false"
		|| val == "0")
		*output = false;
}

void ServerSettings::readIntClientSetting(IQuery * q_get_client_setting, const std::string & name, int * output, bool allow_client_value)
{
	std::string val = readValClientSetting(q_get_client_setting, name, allow_client_value);

	if (!val.empty())
	{
		*output = watoi(val);
	}
}

void ServerSettings::readInt64ClientSetting(IQuery * q_get_client_setting, const std::string & name, int64 * output, bool allow_client_value)
{
	std::string val = readValClientSetting(q_get_client_setting, name, allow_client_value);

	if (!val.empty())
	{
		*output = watoi64(val);
	}
}

void ServerSettings::readSizeClientSetting(IQuery * q_get_client_setting, const std::string & name, size_t * output, bool allow_client_value)
{
	std::string val = readValClientSetting(q_get_client_setting, name, allow_client_value);

	if (!val.empty())
	{
		*output = static_cast<size_t>(watoi64(val));
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

std::vector<std::string> ServerSettings::getBackupVolumes(const std::string& all_volumes, const std::string& all_nonusb_volumes)
{
	std::string vols=getSettings()->image_letters;
	if(strlower(vols)=="all")
	{
		vols=all_volumes;
	}
	else if(strlower(vols)=="all_nonusb")
	{
		vols=all_nonusb_volumes;
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
		std::vector<STimeSpan> add = parseTimeSpan(toks[i]);
		ret.insert(ret.end(), add.begin(), add.end());
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
	Server->secureRandomFill(&key[0], 32);
	return key;
}

int ServerSettings::getUpdateFreqImageIncr()
{
	updateInternal(NULL);
	return static_cast<int>(currentTimeSpanValue(local_settings->update_freq_image_incr)+1);
}

int ServerSettings::getUpdateFreqFileIncr()
{
	updateInternal(NULL);
	return static_cast<int>(currentTimeSpanValue(local_settings->update_freq_incr)+1);
}

int ServerSettings::getUpdateFreqImageFull()
{
	updateInternal(NULL);
	return static_cast<int>(currentTimeSpanValue(local_settings->update_freq_image_full)+1);
}

int ServerSettings::getUpdateFreqFileFull()
{
	updateInternal(NULL);
	return static_cast<int>(currentTimeSpanValue(local_settings->update_freq_full)+1);
}

std::string ServerSettings::getImageFileFormat()
{
	std::string image_file_format = getSettings()->image_file_format;

	return getImageFileFormatInt(image_file_format);
}


std::string ServerSettings::getImageFileFormatInt( const std::string& image_file_format )
{
	if(image_file_format == image_file_format_default)
	{
		if(BackupServer::isImageSnapshotsEnabled()
			|| BackupServer::canReflink())
		{
			return image_file_format_cowraw;
		}
		else
		{
			return image_file_format_default;
		}
	}
	else
	{
		return image_file_format;
	}
}

void ServerSettings::readStringClientSetting(IDatabase * db, int clientid, const std::string & name, const std::string & merge_sep, std::string * output, bool allow_client_value)
{
	std::unique_ptr<ISettingsReader> settings_client, settings_default, settings_global;
	int setting_default_id;
	createSettingsReaders(db, clientid, settings_default, settings_client, settings_global, setting_default_id);

	IQuery* q_get_client_setting = db->Prepare("SELECT value, value_client, use FROM settings_db.settings WHERE clientid=? AND key=?", false);

	if (setting_default_id != 0)
	{
		readStringClientSetting(q_get_client_setting, 0, name, merge_sep, output, allow_client_value);
	}

	readStringClientSetting(q_get_client_setting, setting_default_id, name, merge_sep, output, allow_client_value);
	
	if (settings_client.get() != NULL)
	{
		readStringClientSetting(q_get_client_setting, clientid, name, merge_sep, output, allow_client_value);
	}

	db->destroyQuery(q_get_client_setting);
}


std::vector<STimeSpan> ServerSettings::parseTimeSpan(std::string time_span)
{
	std::vector<STimeSpan> ret;

	std::string c_tok=trim(time_span);
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

				ts.numdays = stop - start + 1;
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

	return ret;
}

std::vector<std::pair<double, STimeSpan > > ServerSettings::parseTimeSpanValue(std::string time_span_value)
{
	std::vector<std::string> toks;

	Tokenize(time_span_value, toks, ";");

	std::vector<std::pair<double, STimeSpan > > ret;


	for(size_t i=0;i<toks.size();++i)
	{
		if(toks[i].find("@")==std::string::npos)
		{
			ret.push_back(std::make_pair(atof(toks[i].c_str()), STimeSpan()));
		}
		else
		{
			double val = atof(getuntil("@", toks[i]).c_str());
			
			std::vector<STimeSpan> timespans = parseTimeSpan(getafter("@", toks[i]));

			for(size_t i=0;i<timespans.size();++i)
			{
				ret.push_back(std::make_pair(val, timespans[i]));
			}
		}
	}

	return ret;
}

int ServerSettings::getLocalSpeed()
{
	return static_cast<int>(round(currentTimeSpanValue(getSettings()->local_speed)));
}

int ServerSettings::getGlobalLocalSpeed()
{
	return static_cast<int>(round(currentTimeSpanValue(getSettings()->global_local_speed)));
}

int ServerSettings::getInternetSpeed()
{
	return static_cast<int>(round(currentTimeSpanValue(getSettings()->internet_speed)));
}

int ServerSettings::getGlobalInternetSpeed()
{
	return static_cast<int>(round(currentTimeSpanValue(getSettings()->global_internet_speed)));
}

std::string ServerSettings::getVirtualClients()
{
	std::string ret = getSettings()->virtual_clients;
	std::string add = getSettings()->virtual_clients_add;
	if (!add.empty())
	{
		if (!ret.empty())ret += "|";
		ret += add;
	}
	return ret;
}

double ServerSettings::currentTimeSpanValue(std::string time_span_value)
{
	std::vector<std::pair<double, STimeSpan > > time_span_values = parseTimeSpanValue(time_span_value);

	double val = 0;
	double selected_time_span_duration=25.f*7;

	for(size_t i=0;i<time_span_values.size();++i)
	{
		std::vector<STimeSpan> single_time_span;
		single_time_span.push_back(time_span_values[i].second);
		if(time_span_values[i].second.duration()<selected_time_span_duration
			&& ( time_span_values[i].second.dayofweek==-1 
			     || isInTimeSpan(single_time_span) ) )
		{
			val = time_span_values[i].first;
			selected_time_span_duration = time_span_values[i].second.duration();
		}
	}

	return val;
}

void ServerSettings::readSettings()
{
	local_settings = new SSettings();
	local_settings->refcount = 1;
	local_settings->clientid = clientid;
	std::unique_ptr<ISettingsReader> settings_client, settings_default, settings_global;
	int settings_default_id;

	IQuery* q_get_client_setting = db->Prepare("SELECT value, value_client, use FROM settings_db.settings WHERE clientid=? AND key=?", false);
	createSettingsReaders(db, clientid, settings_default, settings_client, settings_global, settings_default_id);
	int clientid_backup = clientid;

	ISettingsReader* settings_global_ptr = settings_global.get() != NULL ? settings_global.get() : settings_default.get();
	if (settings_default_id != 0)
	{
		clientid = 0;
		readSettingsDefault(settings_default.get(),
			settings_global_ptr, q_get_client_setting);
		settings_global_ptr = NULL;
	}
	
	clientid = settings_default_id;
	readSettingsDefault(settings_default.get(),
		settings_global_ptr, q_get_client_setting);

	clientid = clientid_backup;

	if (settings_client.get() != NULL)
	{
		readSettingsClient(settings_client.get(), q_get_client_setting);
	}

	db->destroyQuery(q_get_client_setting);
}

namespace
{
	std::string remLeadingZeros(std::string t)
	{
		std::string r;
		bool in=false;
		for(size_t i=0;i<t.size();++i)
		{
			if(!in && t[i]!='0' )
				in=true;

			if(in)
			{
				r+=t[i];
			}
		}
		return r;
	}
}

bool ServerSettings::isInTimeSpan(std::vector<STimeSpan> bw)
{
	if(bw.empty()) return true;
	int dow=atoi(os_strftime("%w").c_str());
	if(dow==0) dow=7;

	float hm=(float)atoi(remLeadingZeros(os_strftime("%H")).c_str())+(float)atoi(remLeadingZeros(os_strftime("%M")).c_str())*(1.f/60.f);
	for(size_t i=0;i<bw.size();++i)
	{
		if(bw[i].dayofweek==dow)
		{
			if( (bw[i].start_hour<=bw[i].stop_hour && hm>=bw[i].start_hour && hm<=bw[i].stop_hour)
				|| (bw[i].start_hour>bw[i].stop_hour && (hm>=bw[i].start_hour || hm<=bw[i].stop_hour) ) )
			{
				return true;
			}
		}
	}

	return false;
}

SLDAPSettings ServerSettings::getLDAPSettings()
{
	std::unique_ptr<ISettingsReader> settings_client, settings_default, settings_global;
	int setting_default_id;
	createSettingsReaders(db, clientid, settings_default, settings_client, settings_global, setting_default_id);
	SLDAPSettings ldap_settings;
	ldap_settings.login_enabled = settings_default->getValue("ldap_login_enabled", "false")=="true";
	
	ldap_settings.server_name = settings_default->getValue("ldap_server_name", "example.com");
	ldap_settings.server_port = settings_default->getValue("ldap_server_port", 3268);
	ldap_settings.username_prefix = settings_default->getValue("ldap_username_prefix", "example\\");
	ldap_settings.username_suffix = settings_default->getValue("ldap_username_suffix", "");
	ldap_settings.group_class_query = settings_default->getValue("ldap_group_class_query", "DC=example,DC=com?memberOf,objectClass?sub?(sAMAccountName={USERNAME})");
	ldap_settings.group_key_name = settings_default->getValue("ldap_group_key_name", "memberOf");
	ldap_settings.class_key_name = settings_default->getValue("ldap_class_key_name", "objectClass");
	ldap_settings.group_rights_map = parseLdapMap(settings_default->getValue("ldap_group_rights_map", "CN=Domain Admins,*==>all=all"));
	ldap_settings.class_rights_map = parseLdapMap(settings_default->getValue("ldap_class_rights_map", "user==>lastacts={AUTOCLIENTS},progress={AUTOCLIENTS},status={AUTOCLIENTS},stop_backup={AUTOCLIENTS},start_backup=all,browse_backups=tokens"));

	return ldap_settings;
}

std::map<std::string, std::string> ServerSettings::parseLdapMap( const std::string& data )
{
	std::vector<std::string> mappings;
	std::map<std::string, std::string> ret;
	Tokenize(data, mappings, "/");
	for(size_t i=0;i<mappings.size();++i)
	{
		std::string source = getuntil("==>", data);
		std::string target = getafter("==>", data);
		ret[source] = target;
	}
	return ret;
}

std::string ServerSettings::ldapMapToString( const std::map<std::string, std::string>& ldap_map )
{
	std::string ret;
	for(std::map<std::string, std::string>::const_iterator it=ldap_map.begin();
		it!=ldap_map.end();++it)
	{
		if(!ret.empty())
		{
			ret+="/";
		}
		ret+=it->first+"==>"+it->second;
	}
	return ret;
}

#endif //CLIENT_ONLY

