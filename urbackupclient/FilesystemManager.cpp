#include "FilesystemManager.h"
#include "../btrfs/btrfsplugin/IBtrfsFactory.h"
#include "DocanyMount.h"
#include "../stringtools.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"

extern IBtrfsFactory* btrfs_fak;
extern IFSImageFactory* image_fak;

std::mutex FilesystemManager::mutex;
std::map<std::string, IBackupFileSystem*> FilesystemManager::filesystems;

bool FilesystemManager::openFilesystemImage(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(path) != filesystems.end())
		return true;

	std::string ext = findextension(path);

	IFile* img;

	if (ext == "img" || ext == "raw")
	{
		img = Server->openFile(path, MODE_RW);
	}
	else if (ext == "vhdx")
	{
		IVHDFile* vhdfile = image_fak->createVHDFile(path, false, 0,
			2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

		if (vhdfile == nullptr || !vhdfile->isOpen())
			return false;

		img = vhdfile;
	}

	if (img == nullptr)
		return false;

	IBackupFileSystem* fs = btrfs_fak->openBtrfsImage(img);
	if (fs == nullptr)
		return false;

	filesystems[path] = fs;

	return true;
}

bool FilesystemManager::mountFileSystem(const std::string& path, const std::string& mount_path)
{
	if (!openFilesystemImage(path))
		return false;

	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(path) == filesystems.end())
	{
		return false;
	}
	
	IBackupFileSystem* fs = filesystems[path];

	return dokany_mount(fs, mount_path);
}

IBackupFileSystem* FilesystemManager::getFileSystem(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	auto it = filesystems.find(path);
	if (it == filesystems.end())
		return nullptr;

	return it->second;
}
