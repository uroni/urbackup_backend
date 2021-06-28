#include "FilesystemManager.h"
#include "../btrfs/btrfsplugin/IBtrfsFactory.h"
#include "DocanyMount.h"
#include "../stringtools.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "../clouddrive/IClouddriveFactory.h"
#include "../common/miniz.h"
#include "../urbackupcommon/backup_url_parser.h"
#include "../urbackupcommon/os_functions.h"

extern IBtrfsFactory* btrfs_fak;
extern IFSImageFactory* image_fak;
extern IClouddriveFactory* clouddrive_fak;

std::mutex FilesystemManager::mutex;
std::map<std::string, IBackupFileSystem*> FilesystemManager::filesystems;

bool FilesystemManager::openFilesystemImage(const std::string& url, const std::string& url_params,
	const str_map& secret_params)
{
	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(url) != filesystems.end())
		return true;

	IFile* img = nullptr;

	IClouddriveFactory::CloudSettings settings;

	if (next(url, 0, "file://"))
	{
		std::string fpath = url.substr(7);
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
	else if (parse_backup_url(url, url_params, secret_params, settings) )
	{
		if (!FileExists(settings.cache_img_path))
		{
			Server->Log("Creating new cache file system", LL_INFO);

			int64 cache_size = 1LL * 1024 * 1024 * 1024;
			std::unique_ptr<IVHDFile> dst_vhdx(image_fak->createVHDFile(settings.cache_img_path + ".new", false, cache_size,
				2 * 1024 * 1024, true, IFSImageFactory::ImageFormat_VHDX));

			if (!dst_vhdx || !dst_vhdx->isOpen())
			{
				Server->Log("Could not open cache vhdx to crate at " + settings.cache_img_path, LL_ERROR);
				return false;
			}

			if (!btrfs_fak->formatVolume(dst_vhdx.get()))
			{
				Server->Log("Could not btrfs format cache volume", LL_ERROR);
				return false;
			}

			dst_vhdx.reset();

			if (!os_rename_file(settings.cache_img_path + ".new",
				settings.cache_img_path))
			{
				Server->Log("Error renaming " + settings.cache_img_path + ". " + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		IVHDFile* cachevhdx = image_fak->createVHDFile(settings.cache_img_path, false, 0,
			2 * 1024 * 1024, false, IFSImageFactory::ImageFormat_VHDX);

		if (cachevhdx == nullptr || !cachevhdx->isOpen())
		{
			Server->Log("Could not open cache vhdx at " + settings.cache_img_path, LL_ERROR);
			return false;
		}

		IBackupFileSystem* cachefs = btrfs_fak->openBtrfsImage(cachevhdx);
		if (cachefs == nullptr)
		{
			Server->Log("Could not open cache btrfs at " + settings.cache_img_path, LL_ERROR);
			return false;
		}
		
		settings.size = 20LL * 1024 * 1024 * 1024; //20GB

		img = clouddrive_fak->createCloudFile(cachefs, settings);
	}

	if (img == nullptr)
		return false;

	if (img->RealSize() == 0)
	{
		if (!btrfs_fak->formatVolume(img))
		{
			Server->Log("Could not btrfs format volume", LL_ERROR);
			return false;
		}
	}

	IBackupFileSystem* fs = btrfs_fak->openBtrfsImage(img);
	if (fs == nullptr)
		return false;

	clouddrive_fak->setTopFs(img, fs);

	filesystems[url] = fs;

	return true;
}

bool FilesystemManager::mountFileSystem(const std::string& url, const std::string& url_params,
	const str_map& secret_params, const std::string& mount_path)
{
	if (!openFilesystemImage(url, url_params, secret_params))
		return false;

	std::lock_guard<std::mutex> lock(mutex);

	if (filesystems.find(url) == filesystems.end())
	{
		return false;
	}
	
	IBackupFileSystem* fs = filesystems[url];

	std::thread dm([url, fs, mount_path]() {
		if (!dokany_mount(fs, mount_path))
		{
			Server->Log("Mounting fs " + url + " at \"" + mount_path + "\" failed", LL_ERROR);
		}
		});

	dm.detach();

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

void FilesystemManager::startupMountFileSystems()
{
	std::vector<SFile> facet_dirs = getFiles("urbackup");

	for (SFile& fdir : facet_dirs)
	{
		if (!fdir.isdir || !next(fdir.name, 0, "data_"))
			continue;

		int facet_id = watoi(getafter("data_", fdir.name));

		std::vector<SFile> settings_files = getFiles("urbackup/" + fdir.name);

		for (SFile& sfn : settings_files)
		{
			if (sfn.isdir ||
				(sfn.name != "settings.cfg" &&
					!next(sfn.name, 0, "settings_")))
				continue;

			std::string clientsubname;
			if (sfn.name != "settings.cfg")
			{
				clientsubname = getbetween("settings_", ".cfg", sfn.name);

				if (clientsubname.empty())
					continue;
			}

			std::string dest, dest_params, computername;
			str_map dest_secret_params;
			size_t max_backups = 1;
			std::string perm_uid;
			if (!ClientConnector::getBackupDest(clientsubname, facet_id, dest,
				dest_params, dest_secret_params, computername, max_backups, perm_uid))
				continue;

			std::string backups_mpath;

			if (facet_id != 1)
				backups_mpath += std::to_string(facet_id);

			if (!clientsubname.empty())
			{
				if (!backups_mpath.empty())
					backups_mpath += "_";

				backups_mpath += clientsubname;
			}

			backups_mpath = "backups" + backups_mpath;


			if (!os_directory_exists(backups_mpath)
				&& !os_create_dir(backups_mpath))
				continue;

			std::string fmpath = os_get_final_path(backups_mpath);

			mountFileSystem(dest, dest_params, dest_secret_params,
				fmpath);
		}
	}
}
