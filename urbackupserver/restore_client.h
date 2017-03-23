#pragma once
#include <string>
#include "serverinterface/backups.h"
#include <vector>
#include "server_log.h"


bool create_clientdl_thread(const std::string& curr_clientname, int curr_clientid, int restore_clientid, std::string foldername, std::string hashfoldername,
	const std::string& filter, bool skip_hashes,
	const std::string& folder_log_name, int64& restore_id, size_t& status_id, logid_t& log_id, const std::string& restore_token, const std::vector<std::pair<std::string, std::string> >& map_paths,
	bool clean_other, bool ignore_other_fs, const std::string& share_path, bool follow_symlinks, int64 restore_flags, THREADPOOL_TICKET& ticket,
	const std::vector<std::string>& tokens, const backupaccess::STokens& access_tokens);

bool create_clientdl_thread(int backupid, const std::string& curr_clientname, int curr_clientid, int64& restore_id, size_t& status_id, logid_t& log_id,
	const std::string& restore_token, const std::vector<std::pair<std::string, std::string> >& map_paths, bool clean_other, bool ignore_other_fs, bool follow_symlinks, int64 restore_flags,
	const std::vector<std::string>& tokens);
