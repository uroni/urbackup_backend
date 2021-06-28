#pragma once
#include <mutex>
#include "../Interface/BackupFileSystem.h"

class FilesystemManager
{
public:
	static bool openFilesystemImage(const std::string& url, const std::string& url_params, 
		const str_map& secret_params);

	static bool mountFileSystem(const std::string& url, const std::string& url_params,
		const str_map& secret_params, const std::string& mount_path);

	static IBackupFileSystem* getFileSystem(const std::string& url);

	static void startupMountFileSystems();
private:

	static std::mutex mutex;
	static std::map<std::string, IBackupFileSystem*> filesystems;
};