#include "ImageMount.h"
#include "../Interface/Server.h"
#include "dao/ServerBackupDao.h"
#include "database.h"
#include "../urbackupcommon/os_functions.h"
#include "../stringtools.h"
#ifdef _WIN32
#include <Windows.h>
#include <Subauth.h>
#include <imdisk.h>
bool os_link_symbolic_junctions_raw(const std::string &target, const std::string &lname);
#endif

namespace
{
	const std::string alt_mount_path = "C:\\UrBackupMounts";

	bool os_mount_image(const std::string& path, int backupid)
	{
		if (path.size() <= 1)
		{
			return false;
		}

		std::wstring filename = Server->ConvertToWchar(path);

		std::vector<char> create_data_buf ( sizeof(IMDISK_CREATE_DATA) + filename.size()*sizeof(wchar_t) );
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

		std::string device_path = Server->ConvertFromWchar(IMDISK_DEVICE_BASE_NAME)+
			convert(static_cast<int64>(create_data->DeviceNumber))+"\\";

		std::string mountpoint = ExtractFilePath(path) + os_file_sep() + "contents";

		if (!os_link_symbolic_junctions_raw(device_path, mountpoint))
		{
			Server->Log("Error creating junction on ImDisk mountpoint at \""+mountpoint+"\". " + os_last_error_str(), LL_WARNING);

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
			&& Server->getTimeMS()-starttime < 60*1000 )
		{
			Server->wait(1000);
		}

		return true;
	}
}

void ImageMount::operator()()
{

}

bool ImageMount::mount_image(int backupid)
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);

	ServerBackupDao::SMountedImage mounted_image = backup_dao.getMountedImage(backupid);
	if (!mounted_image.exists)
	{
		return false;
	}

	backup_dao.setImageMounted(backupid);

	if (!os_mount_image(mounted_image.path, backupid))
	{
		backup_dao.setImageUnmounted(backupid);
		return false;
	}

	return true;
}

std::string ImageMount::get_mount_path(int backupid)
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	ServerBackupDao backup_dao(db);

	ServerBackupDao::SMountedImage mounted_image = backup_dao.getMountedImage(backupid);
	if (!mounted_image.exists)
	{
		return std::string();
	}

	if (mounted_image.mounttime == 0)
	{
		if (!mount_image(backupid))
		{
			return std::string();
		}
	}

	std::string ret = ExtractFilePath(mounted_image.path) + os_file_sep() + "contents";

	if (os_directory_exists(ret))
	{
		return ret;
	}

	ret = alt_mount_path + os_file_sep() + convert(backupid);

	if (os_directory_exists(ret))
	{
		return ret;
	}

	return std::string();
}
