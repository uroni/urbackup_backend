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
		: last_modified(0), created(0)
	{

	}

	FileMetadata(std::string file_permission_bits, int64 last_modified,
		int64 created)
		: file_permission_bits(file_permission_bits),
		last_modified(last_modified), created(created)
	{

	}

	std::string file_permission_bits;
	int64 last_modified;
	int64 created;

	bool operator==(const FileMetadata& other)
	{
		return file_permission_bits==other.file_permission_bits
			&& last_modified== other.last_modified
			&& created == other.created;
	}

	bool bitSet(size_t id) const;

	void serialize(CWData& data) const;

	bool read(CRData& data);

	bool read(str_map& extra_params);
};

bool write_file_metadata(const std::wstring& out_fn, INotEnoughSpaceCallback *cb, const FileMetadata& metadata);

bool is_metadata_only(IFile* hash_file);

bool read_metadata(const std::wstring& in_fn, FileMetadata& metadata);

bool has_metadata(const std::wstring& in_fn, const FileMetadata& metadata);

namespace
{
	const wchar_t* metadata_dir_fn=L".dir_metadata";
}

std::wstring escape_metadata_fn(const std::wstring& fn);

std::wstring unescape_metadata_fn(const std::wstring& fn);

