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

private:
	std::unique_ptr<_FILE_OBJECT> openFileInt(const std::string& path, int mode, bool openDirectory, bool deleteFile);
	bool closeFile(std::unique_ptr<_FILE_OBJECT> file_object);
	bool closeFile(PFILE_OBJECT file_object);
	int64 fileSize(PFILE_OBJECT file_object);

	std::unique_ptr<FsData> fs_data;
	bool has_error = false;
};

void btrfs_fuse_init();

