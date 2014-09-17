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
	ret.push_back(L"file_hash_collect_amount");
	ret.push_back(L"file_hash_collect_timeout");
	ret.push_back(L"file_hash_collect_cachesize");
	ret.push_back(L"end_to_end_file_backup_verification");
	ret.push_back(L"internet_calculate_filehashes_on_client");
	ret.push_back(L"image_file_format");
	ret.push_back(L"internet_connect_always");
	ret.push_back(L"verify_using_client_hashes");
	ret.push_back(L"internet_readd_file_entries");
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
	ret.push_back(L"file_hash_collect_amount");
	ret.push_back(L"file_hash_collect_timeout");
	ret.push_back(L"file_hash_collect_cachesize");
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
	return ret;
}