#include "FilesystemManager.h"
#include "../btrfs/btrfsplugin/IBtrfsFactory.h"

extern IBtrfsFactory* btrfs_fak;

std::mutex FilesystemManager::mutex;
std::map<std::string, IBackupFileSystem*> FilesystemManager::filesystems;

bool FilesystemManager::openFilesystemImage(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(path) != filesystems.end())
		return true;

	IBackupFileSystem* fs = btrfs_fak->openBtrfsImage(path);
	if (fs == nullptr)
		return false;

	filesystems[path] = fs;

	return true;
}

IBackupFileSystem* FilesystemManager::getFileSystem(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	auto it = filesystems.find(path);
	if (it == filesystems.end())
		return nullptr;

	return it->second;
}
