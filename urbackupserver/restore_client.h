#pragma once
#include <string>
#include "serverinterface/backups.h"
#include <vector>


bool create_clientdl_thread(const std::string& curr_clientname, int curr_clientid, int restore_clientid, const std::string& foldername, const std::string& hashfoldername,
	const std::string& filter, bool token_authentication, const std::vector<backupaccess::SToken> &backup_tokens, const std::vector<std::string> &tokens, bool skip_hashes,
	const std::string& folder_log_name);

bool create_clientdl_thread(int backupid, const std::string& curr_clientname, int curr_clientid);
