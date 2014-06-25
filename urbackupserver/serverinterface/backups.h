#pragma once

#include "../file_metadata.h"

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