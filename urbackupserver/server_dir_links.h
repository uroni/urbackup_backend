#pragma once

#include "dao/ServerBackupDao.h"
#include <string>

void init_dir_link_mutex();

void destroy_dir_link_mutex();

bool link_directory_pool(ServerBackupDao& backup_dao, int clientid, const std::wstring& target_dir, const std::wstring& src_dir, const std::wstring& pooldir, bool with_transaction);

bool replay_directory_link_journal(ServerBackupDao& backup_dao);

bool remove_directory_link_dir(const std::wstring &path, ServerBackupDao& backup_dao, int clientid);