#pragma once

#include <string>
#include <vector>
#include "FileServ.h"

std::string map_file(std::string fn, const std::string& identity, bool& allow_exec,
	IFileServ::CbtHashFileInfo* cbt_hash_file_info);
void add_share_path(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec);
bool remove_share_path(const std::string &name, const std::string& identity);
void register_fn_redirect(const std::string & source_fn, const std::string & target_fn);
