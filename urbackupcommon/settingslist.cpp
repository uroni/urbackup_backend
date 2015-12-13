/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "settingslist.h"

std::vector<std::string> getSettingsList(void)
{
	std::vector<std::string> ret;
	ret.push_back("update_freq_incr");
	ret.push_back("update_freq_full");
	ret.push_back("update_freq_image_full");
	ret.push_back("update_freq_image_incr");
	ret.push_back("max_file_incr");
	ret.push_back("min_file_incr");
	ret.push_back("max_file_full");
	ret.push_back("min_file_full");
	ret.push_back("min_image_incr");
	ret.push_back("max_image_incr");
	ret.push_back("min_image_full");
	ret.push_back("max_image_full");
	ret.push_back("startup_backup_delay");
	ret.push_back("backup_window_incr_file");
	ret.push_back("backup_window_full_file");
	ret.push_back("backup_window_incr_image");
	ret.push_back("backup_window_full_image");
	ret.push_back("exclude_files");
	ret.push_back("include_files");
	ret.push_back("computername");
	ret.push_back("virtual_clients");
	ret.push_back("default_dirs");
	ret.push_back("allow_config_paths");
	ret.push_back("allow_starting_full_file_backups");
	ret.push_back("allow_starting_incr_file_backups");
	ret.push_back("allow_starting_full_image_backups");
	ret.push_back("allow_starting_incr_image_backups");
	ret.push_back("allow_pause");
	ret.push_back("allow_log_view");
	ret.push_back("allow_overwrite");
	ret.push_back("allow_tray_exit");
	ret.push_back("image_letters");
	ret.push_back("internet_server");
	ret.push_back("internet_server_port");
	ret.push_back("internet_authkey");
	ret.push_back("internet_speed");
	ret.push_back("local_speed");
	ret.push_back("internet_client_enabled");
	ret.push_back("internet_image_backups");
	ret.push_back("internet_full_file_backups");
	ret.push_back("internet_encrypt");
	ret.push_back("internet_compress");
	ret.push_back("internet_mode_enabled");
	ret.push_back("silent_update");
	ret.push_back("client_quota");
	ret.push_back("local_full_file_transfer_mode");
	ret.push_back("internet_full_file_transfer_mode");
	ret.push_back("local_incr_file_transfer_mode");
	ret.push_back("internet_incr_file_transfer_mode");
	ret.push_back("local_image_transfer_mode");
	ret.push_back("internet_image_transfer_mode");
	ret.push_back("end_to_end_file_backup_verification");
	ret.push_back("internet_calculate_filehashes_on_client");
	ret.push_back("image_file_format");
	ret.push_back("internet_connect_always");
	ret.push_back("server_url");
	ret.push_back("verify_using_client_hashes");
	ret.push_back("internet_readd_file_entries");
	ret.push_back("max_running_jobs_per_client");
	ret.push_back("background_backups");
	ret.push_back("local_full_image_style");
	ret.push_back("internet_incr_image_style");
	ret.push_back("internet_full_image_style");
	return ret;
}

std::vector<std::string> getOnlyServerClientSettingsList(void)
{
	std::vector<std::string> ret;
	ret.push_back("silent_update");
	ret.push_back("client_quota");
	ret.push_back("local_full_file_transfer_mode");
	ret.push_back("internet_full_file_transfer_mode");
	ret.push_back("local_incr_file_transfer_mode");
	ret.push_back("internet_incr_file_transfer_mode");
	ret.push_back("local_image_transfer_mode");
	ret.push_back("internet_image_transfer_mode");
	ret.push_back("end_to_end_file_backup_verification");
	ret.push_back("internet_calculate_filehashes_on_client");
	ret.push_back("image_file_format");
	ret.push_back("verify_using_client_hashes");
	ret.push_back("internet_readd_file_entries");
	ret.push_back("local_incr_image_style");
	ret.push_back("local_full_image_style");
	ret.push_back("background_backups");
	ret.push_back("internet_incr_image_style");
	ret.push_back("internet_full_image_style");
	return ret;
}

std::vector<std::string> getGlobalizedSettingsList(void)
{
	std::vector<std::string> ret;
	ret.push_back("internet_server");
	ret.push_back("internet_server_port");
	ret.push_back("server_url");
	return ret;
}

std::vector<std::string> getLocalizedSettingsList(void)
{
	std::vector<std::string> ret;
	ret.push_back("internet_authkey");
	return ret;
}

std::vector<std::string> getGlobalSettingsList(void)
{
	std::vector<std::string> ret;
	ret.push_back("backupfolder");
	ret.push_back("no_images");
	ret.push_back("no_file_backups");
	ret.push_back("autoshutdown");
	ret.push_back("download_client");
	ret.push_back("autoupdate_clients");
	ret.push_back("max_sim_backups");
	ret.push_back("max_active_clients");
	ret.push_back("cleanup_window");
	ret.push_back("backup_database");
	ret.push_back("internet_server");
	ret.push_back("internet_server_port");
	ret.push_back("global_local_speed");
	ret.push_back("global_internet_speed");
	ret.push_back("use_tmpfiles");
	ret.push_back("use_tmpfiles_images");
	ret.push_back("tmpdir");
	ret.push_back("update_stats_cachesize");
	ret.push_back("global_soft_fs_quota");
	ret.push_back("trust_client_hashes");
	ret.push_back("show_server_updates");
	ret.push_back("server_url");
	ret.push_back("use_incremental_symlinks");
	return ret;
}

std::vector<std::string> getLdapSettingsList(void)
{
	std::vector<std::string> ret;
	ret.push_back("ldap_login_enabled");
	ret.push_back("ldap_server_name");
	ret.push_back("ldap_server_port");
	ret.push_back("ldap_username_prefix");
	ret.push_back("ldap_username_suffix");
	ret.push_back("ldap_group_class_query");
	ret.push_back("ldap_group_key_name");
	ret.push_back("ldap_class_key_name");
	ret.push_back("ldap_group_rights_map");
	ret.push_back("ldap_class_rights_map");
	return ret;
}