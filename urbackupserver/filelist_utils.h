#pragma once

#include "../Interface/File.h"
#include "../urbackupcommon/os_functions.h"

void writeFileRepeat(IFile *f, const char *buf, size_t bsize);

void writeFileRepeat(IFile *f, const std::string &str);

std::string escapeListName( const std::string& listname );

void writeFileItem(IFile* f, SFile cf);


class FileListParser
{
public:
	FileListParser();

	void reset(void);

	bool nextEntry(char ch, SFile &data, std::map<std::wstring, std::wstring>* extra);

private:

	enum ParseState
	{
		ParseState_Type,
		ParseState_Quote,
		ParseState_Name,
		ParseState_NameEscape,
		ParseState_NameFinish,
		ParseState_Filesize,
		ParseState_ModifiedTime,
		ParseState_ExtraParams
	};

	ParseState state;
	std::string t_name;
};
