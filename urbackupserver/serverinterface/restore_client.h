#pragma once
#include <string>
#include "backups.h"
#include <vector>


bool create_clientdl_thread(const std::wstring& clientname, int clientid, const std::wstring& foldername, const std::wstring& hashfoldername,
	const std::wstring& filter, bool token_authentication, const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes,
	const std::wstring& folder_log_name);
