#pragma once

#include "../../urbackupcommon/file_metadata.h"
#include "../../urbackupcommon/json.h"
#include "../../Interface/Database.h"
#include <string>
#include <vector>

namespace backupaccess
{
	std::wstring getBackupFolder(IDatabase* db);

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

	STokens readTokens(const std::wstring& backupfolder, const std::wstring& clientname, const std::wstring& path);

	bool checkFileToken( const std::vector<SToken> &backup_tokens, const std::vector<std::string> &tokens, const FileMetadata &metadata );

	std::string decryptTokens(IDatabase* db, const str_map& GET);

	std::wstring get_backup_path(IDatabase* db, int backupid, int t_clientid);

	JSON::Array get_backups_with_tokens(IDatabase * db, int t_clientid, std::wstring clientname, std::string* fileaccesstokens );

	struct SPathInfo
	{
		SPathInfo()
			: can_access_path(false)
		{

		}

		STokens backup_tokens;
		bool can_access_path;

		std::wstring rel_metadata_path;
		std::wstring rel_path;

		std::wstring full_metadata_path;
		std::wstring full_path;
	};

	SPathInfo get_metadata_path_with_tokens(const std::wstring& u_path, bool is_file, std::string* fileaccesstokens,
		std::wstring clientname, std::wstring backupfolder, int* backupid, std::wstring backuppath);

	bool get_files_with_tokens(IDatabase* db, int* backupid, int t_clientid, std::wstring clientname,
		std::string* fileaccesstokens, const std::wstring& u_path, bool is_file, JSON::Object& ret);
}

