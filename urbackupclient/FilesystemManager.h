#pragma once
#include <mutex>
#include "../btrfs/btrfsplugin/IBackupFileSystem.h"

class FilesystemManager
{
public:
	static bool openFilesystemImage(const std::string& path);

	static bool mountFileSystem(const std::string& path, const std::string& mount_path);

	static IBackupFileSystem* getFileSystem(const std::string& path);

private:

	static std::mutex mutex;
	static std::map<std::string, IBackupFileSystem*> filesystems;
};