#include "FilesystemManager.h"
#include "../btrfs/btrfsplugin/IBtrfsFactory.h"
#include "DocanyMount.h"
#include "../stringtools.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../clouddrive/IClouddriveFactory.h"

extern IBtrfsFactory* btrfs_fak;
extern IFSImageFactory* image_fak;
extern IClouddriveFactory* clouddrive_fak;

std::mutex FilesystemManager::mutex;
std::map<std::string, IBackupFileSystem*> FilesystemManager::filesystems;

bool FilesystemManager::openFilesystemImage(const std::string& path)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(path) != filesystems.end())
		return true;

	IFile* img;

	if (next(path, 0, "file://"))
	{
		std::string fpath = path.substr(7);
		std::string ext = findextension(fpath);
		if (ext == "img" || ext == "raw")
		{
			img = Server->openFile(fpath, MODE_RW);
		}
		else if (ext == "vhdx")
		{
			IVHDFile* vhdfile = image_fak->createVHDFile(fpath, false, 0,
				2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

			if (vhdfile == nullptr || !vhdfile->isOpen())
				return false;

			img = vhdfile;
		}
	}
	else if (next(path, 0, "s3://"))
	{
		IClouddriveFactory::CloudSettings settings;
		settings.endpoint = IClouddriveFactory::CloudEndpoint::S3;

		std::string authorization = getbetween("s3://", "@", path);
		std::string server = getafter("@", path);
		if (server.empty())
		{
			server = path.substr(5);
		}

		settings.s3_settings.access_key = getuntil(":", authorization);
		settings.s3_settings.secret_access_key = getafter(":", authorization);

		settings.s3_settings.endpoint = getuntil("/", server);
		settings.s3_settings.bucket_name = getafter("/", server);

		std::string nurl = "s3://" + server;
		std::string cacheid = Server->GenerateHexMD5(nurl);
		std::string cache_img_path = "urbackup/" + cacheid + ".vhdx";
		settings.s3_settings.cache_db_path = "urbackup/" + cacheid + ".db";

		if (!FileExists(cache_img_path))
		{
			if (!copy_file("urbackup/cache_init.vhdx", cache_img_path))
			{
				Server->Log("Could not copy urbackup/cache_init.vhdx to " + cache_img_path + ". " + os_last_error_str(),
					LL_ERROR);
				return false;
			}
		}

		IVHDFile* cachevhdx = image_fak->createVHDFile(cache_img_path, false, 0,
			2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

		if (cachevhdx == nullptr || !cachevhdx->isOpen())
		{
			Server->Log("Could not open cache vhdx at " + cache_img_path, LL_ERROR);
			return false;
		}

		IBackupFileSystem* cachefs = btrfs_fak->openBtrfsImage(cachevhdx);
		if (cachefs == nullptr)
		{
			Server->Log("Could not open cache btrfs at " + cache_img_path, LL_ERROR);
			return false;
		}

		
		settings.size = 100LL * 1024 * 1024 * 1024 * 1024; //100TB

		img = clouddrive_fak->createCloudFile(cachefs, settings);
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
