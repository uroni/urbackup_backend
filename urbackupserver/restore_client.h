#pragma once
#include <string>
#include "serverinterface/backups.h"
#include <vector>


bool create_clientdl_thread(const std::wstring& curr_clientname, int curr_clientid, int restore_clientid, const std::wstring& foldername, const std::wstring& hashfoldername,
	const std::wstring& filter, bool token_authentication, const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes,
	const std::wstring& folder_log_name);

bool create_clientdl_thread(int backupid, const std::wstring& curr_clientname, int curr_clientid);