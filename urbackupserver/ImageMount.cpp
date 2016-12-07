#include "ImageMount.h"
#include "../Interface/Server.h"
#include "dao/ServerBackupDao.h"
#include "database.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#include <assert.h>
#ifdef _WIN32
#include <Windows.h>
#include <Subauth.h>
#include <imdisk.h>
bool os_link_symbolic_junctions_raw(const std::string &target, const std::string &lname);
#endif

std::map<int, size_t> ImageMount::mounted_images;
IMutex* ImageMount::mounted_images_mutex;
std::set<int> ImageMount::locked_images;

namespace
{
#ifdef _WIN32
	const std::string alt_mount_path = "C:\\UrBackupMounts";

	bool os_mount_image(const std::string& path, int backupid)
	{
		if (path.size() <= 1)
		{
			return false;
		}

		std::wstring filename = Server->ConvertToWchar(path);

		std::vector<char> create_data_buf(sizeof(IMDISK_CREATE_DATA) + filename.size() * sizeof(wchar_t));
		IMDISK_CREATE_DATA* create_data = reinterpret_cast<IMDISK_CREATE_DATA*>(create_data_buf.data());

		create_data->FileNameLength = static_cast<USHORT>(filename.size() * sizeof(wchar_t));
		memcpy(create_data->FileName, filename.c_str(), create_data->FileNameLength);
		create_data->DeviceNumber = IMDISK_AUTO_DEVICE_NUMBER;
		create_data->Flags = IMDISK_TYPE_PROXY | IMDISK_PROXY_TYPE_TCP | IMDISK_OPTION_RO;


		HANDLE hDriver = CreateFile(IMDISK_CTL_DOSDEV_NAME,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);

		if (hDriver == INVALID_HANDLE_VALUE)
		{
			Server->Log("Error communicating with ImDisk driver. " + os_last_error_str(), LL_ERROR);
			return false;
		}

		DWORD bytes_returned;
		DWORD buffer_size = static_cast<DWORD>(create_data_buf.size());
		if (!DeviceIoControl(hDriver, IOCTL_IMDISK_CREATE_DEVICE,
			create_data, buffer_size, create_data,
			buffer_size, &bytes_returned, NULL))
		{
			Server->Log("Error creating ImDisk device. " + os_last_error_str(), LL_ERROR);
			return false;
		}

		CloseHandle(hDriver);

		std::string device_path = Server->ConvertFromWchar(IMDISK_DEVICE_BASE_NAME) +
			convert(static_cast<int64>(create_data->DeviceNumber)) + "\\";

		std::string mountpoint = ExtractFilePath(path) + os_file_sep() + "contents";

		if (!os_link_symbolic_junctions_raw(device_path, mountpoint))
		{
			Server->Log("Error creating junction on ImDisk mountpoint at \"" + mountpoint + "\". " + os_last_error_str(), LL_WARNING);

			mountpoint = alt_mount_path + os_file_sep() + convert(backupid);

			if (!((os_directory_exists(alt_mount_path) || os_create_dir_recursive(alt_mount_path))
				&& os_link_symbolic(device_path, mountpoint)))
			{
				Server->Log("Error creating junction on ImDisk mountpoint. " + os_last_error_str(), LL_ERROR);
				return false;
			}
		}

		int64 starttime = Server->getTimeMS();
		bool has_error = false;
		while (getFiles(mountpoint, &has_error).empty()
			&& has_error
			&& Server->getTimeMS() - starttime < 60 * 1000)
		{
			Server->wait(1000);
		}

		return true;
	}

	bool os_unmount_image(const std::string& mountpoint, const std::string& path, int backupid)
	{
		std::string target;
		if (!os_get_symlink_target(mountpoint, target)
			|| target.empty())
		{
			Server->Log("Error getting mountpoint target. " + os_last_error_str(), LL_ERROR);
			return false;
		}

		if (target[target.size() - 1] == '\\')
		{
			target.erase(target.size() - 1, 1);
		}

		HANDLE hDevice = CreateFile(Server->ConvertToWchar("\\\\?\\GLOBALROOT" + target).c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);

		if (hDevice == INVALID_HANDLE_VALUE)
		{
			Server->Log("Cannot open ImDisk device " + target + ". " + os_last_error_str(), LL_WARNING);
			os_remove_symlink_dir(mountpoint);
			return false;
		}

		DWORD bytes_returned;
		if (!DeviceIoControl(hDevice, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytes_returned, NULL))
		{
			Server->Log("Locking ImDisk device failed. " + os_last_error_str() + ". Forcing unmount...", LL_WARNING);
			DeviceIoControl(hDevice, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytes_returned, NULL);
			DeviceIoControl(hDevice, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytes_returned, NULL);
		}
		else
		{
			if (!DeviceIoControl(hDevice, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytes_returned, NULL))
			{
				Server->Log("Dismounting volume failed. " + os_last_error_str(), LL_ERROR);
			}
		}

		if (!DeviceIoControl(hDevice, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &bytes_returned, NULL))
		{
			Server->Log("Ejecting ImDisk failed. " + os_last_error_str() + ". Forcing removal...", LL_WARNING);

			std::string device_number = getafter(Server->ConvertFromWchar(IMDISK_DEVICE_BASE_NAME), target);
			HANDLE hDriver = CreateFile(IMDISK_CTL_DOSDEV_NAME,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL,
				OPEN_EXISTING,
				0,
				NULL);

			if (hDriver == INVALID_HANDLE_VALUE)
			{
				CloseHandle(hDevice);
				Server->Log("Error communicating with ImDisk driver (2). " + os_last_error_str(), LL_ERROR);
				os_remove_symlink_dir(mountpoint);
				return false;
			}

			DWORD deviceNumber = watoi(device_number);
			if (!DeviceIoControl(hDriver, IOCTL_IMDISK_REMOVE_DEVICE,
				&deviceNumber, sizeof(deviceNumber), NULL,
				0, &bytes_returned, NULL))
			{
				Server->Log("Error removing ImDisk device. " + os_last_error_str(), LL_ERROR);
				CloseHandle(hDevice);
				CloseHandle(hDriver);
				os_remove_symlink_dir(mountpoint);
				return false;
			}

			CloseHandle(hDriver);
		}

		DeviceIoControl(hDevice, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytes_returned, NULL);
		CloseHandle(hDevice);

		return os_remove_symlink_dir(mountpoint);
	}
#else
	bool image_helper_action(const std::string& path, int backupid, const std::string& action)
	{
		IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
		ServerBackupDao backup_dao(db);

		ServerBackupDao::CondString clientname = backup_dao.getClientnameByImageid(backupid);
		if (!clientname.exists)
		{
			return false;
		}

		std::string foldername = ExtractFileName(ExtractFilePath(path));
		std::string imagename = ExtractFileName(path);

		return system(("urbackup_mount_helper " + action + " \"" + clientname.value + "\" \"" + foldername + "\" \"" + imagename + "\"").c_str()) == 0;
	}

	bool os_mount_image(const std::string& path, int backupid)
	{
		return image_helper_action(path, backupid, "mount");
	}

	bool os_unmount_image(const std::string& mountpoint, const std::string& path, int backupid)
	{
		return image_helper_action(path, backupid, "umount");
	}
#endif


	class ScopedLockImage
	{
		int backupid;

	public:
		ScopedLockImage(int backupid)
			: backupid(backupid)
		{
			ImageMount::lockImage(backupid);
		}

		~ScopedLockImage()
		{
			if (backupid != 0)
			{
				ImageMount::unlockImage(backupid);
			}
		}

		void reset(int bid = 0)
		{
			if (backupid != 0)
			{
				ImageMount::unlockImage(backupid);
			}
			backupid = bid;
			if (backupid != 0)
			{
				ImageMount::lockImage(backupid);
			}
		}
	};

	bool unmount_image(ServerBackupDao& backup_dao, int backupid)
	{
		ServerBackupDao::SMountedImage image_inf = backup_dao.getMountedImage(backupid);
		if (!image_inf.exists)
		{
			return false;
		}

		std::string mountpoint = ExtractFilePath(image_inf.path) + os_file_sep() + "contents";

#ifdef _WIN32
		if (!os_directory_exists(mountpoint))
		{
			mountpoint = alt_mount_path + os_file_sep() + convert(backupid);
		}

		if (!os_directory_exists(mountpoint))
		{
			return false;
		}
#endif

		return os_unmount_image(mountpoint, image_inf.path, backupid);
	}
}

void ImageMount::operator()()
{
	mounted_images_mutex = Server->createMutex();

	Server->waitForStartupComplete();

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);
	
	int waittimems = 0;
	int64 oldtimes = 0;
	while (true)
	{
		Server->wait(waittimems);

		std::vector<ServerBackupDao::SMountedImage> old_mounted_images =
			backup_dao.getOldMountedImages(oldtimes);

		waittimems = 60 * 1000;
		oldtimes = 5 * 60;

		IScopedLock lock(mounted_images_mutex);
		for (size_t i = 0; i < old_mounted_images.size(); ++i)
		{
			if (mounted_images.find(old_mounted_images[i].id) == mounted_images.end()
				&& locked_images.find(old_mounted_images[i].id) == locked_images.end())
			{
				locked_images.insert(old_mounted_images[i].id);
				lock.relock(NULL);

				int64 passed_time_s = Server->getTimeSeconds() - old_mounted_images[i].mounttime;
				Server->Log("Unmounting mounted image backup id " + convert(old_mounted_images[i].id) +
					" path \"" + old_mounted_images[i].path + "\" mounted " + PrettyPrintTime(passed_time_s*1000) + " ago", LL_INFO);
				unmount_image(backup_dao, old_mounted_images[i].id);
				backup_dao.setImageUnmounted(old_mounted_images[i].id);

				lock.relock(mounted_images_mutex);
				locked_images.erase(locked_images.find(old_mounted_images[i].id));
			}
		}
	}
}

bool ImageMount::mount_image(int backupid, ScopedMountedImage& mounted_image)
{
	ScopedLockImage lock_image(backupid);

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);

	ServerBackupDao::SMountedImage image_inf = backup_dao.getMountedImage(backupid);
	if (!image_inf.exists)
	{
		return false;
	}

	backup_dao.setImageMounted(backupid);
	mounted_image.reset(backupid);

	if (!os_mount_image(image_inf.path, backupid))
	{
		backup_dao.setImageUnmounted(backupid);
		mounted_image.reset();
		return false;
	}

	return true;
}

std::string ImageMount::get_mount_path(int backupid, bool do_mount, ScopedMountedImage& mounted_image)
{
	ScopedLockImage lock_image(backupid);

	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);

	ServerBackupDao::SMountedImage image_inf = backup_dao.getMountedImage(backupid);
	if (!image_inf.exists)
	{
		return std::string();
	}

	if (image_inf.mounttime == 0)
	{
		if (do_mount)
		{
			lock_image.reset();
			if (!mount_image(backupid, mounted_image))
			{
				return std::string();
			}
			lock_image.reset(backupid);
		}
		else
		{
			return std::string();
		}
	}

	std::string ret = ExtractFilePath(image_inf.path) + os_file_sep() + "contents";

	if (os_directory_exists(ret))
	{
		mounted_image.reset(backupid);
		backup_dao.setImageMounted(backupid);
		return ret;
	}

#ifdef _WIN32
	ret = alt_mount_path + os_file_sep() + convert(backupid);

	if (os_directory_exists(ret))
	{
		mounted_image.reset(backupid);
		backup_dao.setImageMounted(backupid);
		return ret;
	}
#endif

	return std::string();
}

void ImageMount::incrImageMounted(int backupid)
{
	IScopedLock lock(mounted_images_mutex);
	++mounted_images[backupid];
}

void ImageMount::decrImageMounted(int backupid)
{
	IScopedLock lock(mounted_images_mutex);
	std::map<int, size_t>::iterator it = mounted_images.find(backupid);
	assert(it != mounted_images.end()
		&& it->second > 0);
	if (it == mounted_images.end())
	{
		return;
	}
	--it->second;
	if (it->second == 0)
	{
		mounted_images.erase(it);
	}
}

void ImageMount::lockImage(int backupid)
{
	IScopedLock lock(mounted_images_mutex);
	while (locked_images.find(backupid) != locked_images.end())
	{
		lock.relock(NULL);
		Server->wait(100);
		lock.relock(mounted_images_mutex);
	}

	locked_images.insert(backupid);
}

void ImageMount::unlockImage(int backupid)
{
	IScopedLock lock(mounted_images_mutex);
	locked_images.erase(locked_images.find(backupid));
}
