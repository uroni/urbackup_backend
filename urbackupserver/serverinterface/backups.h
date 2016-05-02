#pragma once

#include "../../urbackupcommon/file_metadata.h"
#include "../../urbackupcommon/json.h"
#include "../../Interface/Database.h"
#include <string>
#include <vector>

namespace backupaccess
{
	std::string getBackupFolder(IDatabase* db);

	std::string constructFilter(const std::vector<int> &clientid, std::string key);

	struct SToken
	{
		int64 id;
		std::string username;
		std::string token;
	};

	struct STokens
	{
		std::string access_key;
		std::vector<SToken> tokens;
	};	

	STokens readTokens(const std::string& backupfolder, const std::string& clientname, const std::string& path);

	bool checkFileToken( const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, const FileMetadata &metadata );

	std::string decryptTokens(IDatabase* db, const str_map& GET);

	std::string get_backup_path(IDatabase* db, int backupid, int t_clientid);

	JSON::Array get_backups_with_tokens(IDatabase * db, int t_clientid, std::string clientname, std::string* fileaccesstokens, int backupid_offset, bool& has_access);

	struct SPathInfo
	{
		SPathInfo()
			: can_access_path(false), is_file(false), is_symlink(false)
		{

		}

		STokens backup_tokens;
		bool can_access_path;

		std::string rel_metadata_path;
		std::string rel_path;

		std::string full_metadata_path;
		std::string full_path;

		bool is_file;
		bool is_symlink;
	};

	SPathInfo get_metadata_path_with_tokens(const std::string& u_path, std::string* fileaccesstokens,
		std::string clientname, std::string backupfolder, int* backupid, std::string backuppath);

	bool get_files_with_tokens(IDatabase* db, int* backupid, int t_clientid, std::string clientname,
        std::string* fileaccesstokens, const std::string& u_path, int backupid_offset, JSON::Object& ret);
}

