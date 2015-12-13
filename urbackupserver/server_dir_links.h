#pragma once

#include "dao/ServerBackupDao.h"
#include <string>

void init_dir_link_mutex();

void destroy_dir_link_mutex();

std::string escape_glob_sql(const std::string& glob);

bool link_directory_pool(ServerBackupDao& backup_dao, int clientid, const std::string& target_dir, const std::string& src_dir, const std::string& pooldir, bool with_transaction);

bool replay_directory_link_journal(ServerBackupDao& backup_dao);

bool remove_directory_link_dir(const std::string &path, ServerBackupDao& backup_dao, int clientid, bool delete_root=true, bool with_transaction=true);
