#pragma once
#include <string>
#include "../../Interface/File.h"
#include <memory>
#include "../../urbackupcommon/os_functions.h"
#include "fuse_t.h"

struct _DEVICE_OBJECT;
typedef struct _DEVICE_OBJECT* PDEVICE_OBJECT;

struct _FILE_OBJECT;
typedef struct _FILE_OBJECT* PFILE_OBJECT;

int main();

void register_new_disk(PDEVICE_OBJECT disk);

struct FsData;

class BtrfsFuse
{
public:
	BtrfsFuse(IFile* img);
	~BtrfsFuse();

	BtrfsFuse(const BtrfsFuse&) = delete;
	void operator=(const BtrfsFuse&) = delete;

	bool createDir(const std::string& path);
	bool deleteFile(const std::string& path);
	IFsFile* openFile(const std::string& path, int mode);

	int getFileType(const std::string& path);

	bool reflink(const std::string& src_path,
		const std::string& dest_path);

	bool flush();

	std::vector<SFile> listFiles(const std::string& path);

	bool create_subvol(const std::string& path);

	bool create_snapshot(const std::string& src_path,
		const std::string& dest_path);

	bool rename(const std::string& orig_name, const std::string& new_name);

	bool link_symbolic(const std::string& target, const std::string& lname);

	bool get_has_error();

	bool resize_max();

	struct SpaceInfo
	{
		int64 metadata_allocated;
		int64 metadata_used;
		int64 data_allocated;
		int64 data_used;

		int64 unallocated;
		int64 used;
		int64 allocated;
	};

	SpaceInfo get_space_info();

	int64 get_total_space();

	str_map get_xattrs(const std::string& path);

	bool set_xattr(const std::string& path, const std::string& tkey, const std::string& tval);

	std::string errno_to_str(int rc);

	struct SBtrfsChunk
	{
		uint64_t offset;
		uint64_t len;
		int metadata;
	};

	std::pair<SBtrfsChunk*, size_t> get_chunks();

private:
	std::unique_ptr<_FILE_OBJECT> openFileInt(const std::string& path, int mode, bool openDirectory, bool deleteFile);
	bool closeFile(std::unique_ptr<_FILE_OBJECT> file_object);
	bool closeFile(PFILE_OBJECT file_object);
	int64 fileSize(PFILE_OBJECT file_object);

	std::unique_ptr<FsData> fs_data;
	bool has_error = false;
	IFile* img;
};

void btrfs_fuse_init();

bool btrfs_format_vol(IFile* vol);

