#pragma once

#include <string>
#include "../Interface/Types.h"
#include "../Interface/File.h"
#include "../stringtools.h"

class INotEnoughSpaceCallback;
class CWData;
class CRData;

class FileMetadata
{
public:
	FileMetadata()
		: last_modified(0), created(0), exist(false)
	{

	}

	FileMetadata(std::string file_permissions, int64 last_modified,
		int64 created)
		: file_permissions(file_permissions),
		last_modified(last_modified), created(created)
	{

	}

	std::string file_permissions;
	int64 last_modified;
	int64 created;
	std::string shahash;
	bool exist;

	bool operator==(const FileMetadata& other)
	{
		return file_permissions==other.file_permissions
			&& last_modified== other.last_modified
			&& created == other.created;
	}

	bool hasPermission(int id, bool& denied) const;

	void serialize(CWData& data) const;

	bool read(CRData& data);

	bool read(str_map& extra_params);

	void set_shahash(const std::string& the_shahash);
};

bool write_file_metadata(const std::wstring& out_fn, INotEnoughSpaceCallback *cb, const FileMetadata& metadata);

bool write_file_metadata(IFile* out, INotEnoughSpaceCallback *cb, const FileMetadata& metadata);

bool is_metadata_only(IFile* hash_file);

bool read_metadata(const std::wstring& in_fn, FileMetadata& metadata);

bool has_metadata(const std::wstring& in_fn, const FileMetadata& metadata);

namespace
{
	const wchar_t* metadata_dir_fn=L".dir_metadata";
}

std::wstring escape_metadata_fn(const std::wstring& fn);

std::wstring unescape_metadata_fn(const std::wstring& fn);

