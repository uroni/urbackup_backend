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
	ret.push_back(L"backup_window");
	ret.push_back(L"exclude_files");
	ret.push_back(L"include_files");
	ret.push_back(L"computername");
	ret.push_back(L"default_dirs");
	ret.push_back(L"allow_config_paths");
	ret.push_back(L"allow_starting_file_backups");
	ret.push_back(L"allow_starting_image_backups");
	ret.push_back(L"allow_pause");
	ret.push_back(L"allow_log_view");
	ret.push_back(L"image_letters");
	ret.push_back(L"internet_server");
	ret.push_back(L"internet_server_port");
	ret.push_back(L"internet_authkey");
	return ret;
}