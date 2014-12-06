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

#include "settingslist.h"

std::vector<std::wstring> getSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"update_freq_incr");
	ret.push_back(L"update_freq_full");
	ret.push_back(L"update_freq_image_full");
	ret.push_back(L"update_freq_image_incr");
	ret.push_back(L"max_file_incr");
	ret.push_back(L"min_file_incr");
	ret.push_back(L"max_file_full");
	ret.push_back(L"min_file_full");
	ret.push_back(L"min_image_incr");
	ret.push_back(L"max_image_incr");
	ret.push_back(L"min_image_full");
	ret.push_back(L"max_image_full");
	ret.push_back(L"startup_backup_delay");
	ret.push_back(L"backup_window_incr_file");
	ret.push_back(L"backup_window_full_file");
	ret.push_back(L"backup_window_incr_image");
	ret.push_back(L"backup_window_full_image");
	ret.push_back(L"exclude_files");
	ret.push_back(L"include_files");
	ret.push_back(L"computername");
	ret.push_back(L"default_dirs");
	ret.push_back(L"allow_config_paths");
	ret.push_back(L"allow_starting_full_file_backups");
	ret.push_back(L"allow_starting_incr_file_backups");
	ret.push_back(L"allow_starting_full_image_backups");
	ret.push_back(L"allow_starting_incr_image_backups");
	ret.push_back(L"allow_pause");
	ret.push_back(L"allow_log_view");
	ret.push_back(L"allow_overwrite");
	ret.push_back(L"allow_tray_exit");
	ret.push_back(L"image_letters");
	ret.push_back(L"internet_server");
	ret.push_back(L"internet_server_port");
	ret.push_back(L"internet_authkey");
	ret.push_back(L"internet_speed");
	ret.push_back(L"local_speed");
	ret.push_back(L"internet_client_enabled");
	ret.push_back(L"internet_image_backups");
	ret.push_back(L"internet_full_file_backups");
	ret.push_back(L"internet_encrypt");
	ret.push_back(L"internet_compress");
	ret.push_back(L"internet_mode_enabled");
	ret.push_back(L"silent_update");
	ret.push_back(L"client_quota");
	ret.push_back(L"local_full_file_transfer_mode");
	ret.push_back(L"internet_full_file_transfer_mode");
	ret.push_back(L"local_incr_file_transfer_mode");
	ret.push_back(L"internet_incr_file_transfer_mode");
	ret.push_back(L"local_image_transfer_mode");
	ret.push_back(L"internet_image_transfer_mode");
	ret.push_back(L"end_to_end_file_backup_verification");
	ret.push_back(L"internet_calculate_filehashes_on_client");
	ret.push_back(L"image_file_format");
	ret.push_back(L"internet_connect_always");
	ret.push_back(L"server_url");
	ret.push_back(L"verify_using_client_hashes");
	ret.push_back(L"internet_readd_file_entries");
	ret.push_back(L"max_running_jobs_per_client");
	return ret;
}

std::vector<std::wstring> getOnlyServerClientSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"silent_update");
	ret.push_back(L"client_quota");
	ret.push_back(L"local_full_file_transfer_mode");
	ret.push_back(L"internet_full_file_transfer_mode");
	ret.push_back(L"local_incr_file_transfer_mode");
	ret.push_back(L"internet_incr_file_transfer_mode");
	ret.push_back(L"local_image_transfer_mode");
	ret.push_back(L"internet_image_transfer_mode");
	ret.push_back(L"end_to_end_file_backup_verification");
	ret.push_back(L"internet_calculate_filehashes_on_client");
	ret.push_back(L"image_file_format");
	ret.push_back(L"verify_using_client_hashes");
	ret.push_back(L"internet_readd_file_entries");
	return ret;
}

std::vector<std::wstring> getGlobalizedSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"internet_server");
	ret.push_back(L"internet_server_port");
	ret.push_back(L"server_url");
	return ret;
}

std::vector<std::wstring> getLocalizedSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"internet_authkey");
	return ret;
}

std::vector<std::wstring> getGlobalSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"backupfolder");
	ret.push_back(L"no_images");
	ret.push_back(L"no_file_backups");
	ret.push_back(L"autoshutdown");
	ret.push_back(L"download_client");
	ret.push_back(L"autoupdate_clients");
	ret.push_back(L"max_sim_backups");
	ret.push_back(L"max_active_clients");
	ret.push_back(L"cleanup_window");
	ret.push_back(L"backup_database");
	ret.push_back(L"internet_server");
	ret.push_back(L"internet_server_port");
	ret.push_back(L"global_local_speed");
	ret.push_back(L"global_internet_speed");
	ret.push_back(L"use_tmpfiles");
	ret.push_back(L"use_tmpfiles_images");
	ret.push_back(L"tmpdir");
	ret.push_back(L"update_stats_cachesize");
	ret.push_back(L"global_soft_fs_quota");
	ret.push_back(L"filescache_type");
	ret.push_back(L"filescache_size");
	ret.push_back(L"trust_client_hashes");
	ret.push_back(L"show_server_updates");
	ret.push_back(L"server_url");
	ret.push_back(L"use_incremental_symlinks");
	return ret;
}

std::vector<std::wstring> getLdapSettingsList(void)
{
	std::vector<std::wstring> ret;
	ret.push_back(L"ldap_login_enabled");
	ret.push_back(L"ldap_server_name");
	ret.push_back(L"ldap_server_port");
	ret.push_back(L"ldap_username_prefix");
	ret.push_back(L"ldap_username_suffix");
	ret.push_back(L"ldap_group_class_query");
	ret.push_back(L"ldap_group_key_name");
	ret.push_back(L"ldap_class_key_name");
	ret.push_back(L"ldap_group_rights_map");
	ret.push_back(L"ldap_class_rights_map");
	return ret;
}