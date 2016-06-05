#pragma once

#include "dao/ServerLinkDao.h"
#include "dao/ServerLinkJournalDao.h"
#include <string>
#include <memory>

void init_dir_link_mutex();

void destroy_dir_link_mutex();

std::string escape_glob_sql(const std::string& glob);

bool link_directory_pool(int clientid, const std::string& target_dir, const std::string& src_dir, const std::string& pooldir, bool with_transaction,
	ServerLinkDao*& link_dao, ServerLinkJournalDao*& link_journal_dao);

bool replay_directory_link_journal();

bool remove_directory_link(const std::string &path, ServerLinkDao& link_dao, int clientid,
	std::auto_ptr<DBScopedSynchronous>& synchronous_link_dao, bool with_transaction=true);

bool remove_directory_link_dir(const std::string &path, ServerLinkDao& link_dao, int clientid, bool delete_root=true, bool with_transaction=true);

bool is_directory_link(const std::string& path);