/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "snapshot_helper.h"
#include "../Interface/Server.h"
#include <stdlib.h>
#include "../stringtools.h"
#include "server.h"
#include "../urbackupcommon/os_functions.h"
#ifdef _WIN32
#include "server_settings.h"
#include "database.h"
#include <winternl.h>
#define WEXITSTATUS(x) x
#else
#include <sys/types.h>
#include <sys/wait.h>
#endif

std::string SnapshotHelper::helper_name="urbackup_snapshot_helper";

int SnapshotHelper::isAvailable(void)
{
#ifdef _WIN32
	setupWindows();
	return testWindows();
#else

	int rc=system((helper_name+" test").c_str());

	rc = WEXITSTATUS(rc);
	if (rc >= 10)
	{
		return rc - 10;
	}

	return -1;
#endif
}

bool SnapshotHelper::createEmptyFilesystem(bool image, std::string clientname, std::string name, std::string& errmsg)
{
#ifdef _WIN32
	return createEmptyFilesystemWindows(clientname, name, errmsg);
#else
	int rc=os_popen((helper_name + " " + convert(BackupServer::getSnapshotMethod(image)) + " create \""+(clientname)+"\" \""+(name)+"\" 2>&1").c_str(), errmsg);
	return rc==0;
#endif
}

bool SnapshotHelper::snapshotFileSystem(bool image, std::string clientname, std::string old_name, std::string snapshot_name, std::string& errmsg)
{
#ifdef _WIN32
	return snapshotFileSystemWindows(clientname, old_name, snapshot_name, errmsg);
#else
	int rc=os_popen((helper_name + " " + convert(BackupServer::getSnapshotMethod(image)) + " snapshot \""+(clientname)+"\" \""+(old_name)+"\" \""+(snapshot_name)+"\" 2>&1").c_str(), errmsg);
	return rc==0;
#endif
}

bool SnapshotHelper::removeFilesystem(bool image, std::string clientname, std::string name)
{
#ifdef _WIN32
	return removeFilesystemWindows(clientname, name);
#else
	if (!image &&
		BackupServer::getSnapshotMethod(image) == BackupServer::ESnapshotMethod_ZfsFile &&
		name.find(".startup-del") != std::string::npos)
		name = greplace(".startup-del", "", name);

	int rc=system((helper_name + " " + convert(BackupServer::getSnapshotMethod(image)) + " remove \""+clientname+"\" \""+name+"\"").c_str());
	return rc==0;
#endif
}

bool SnapshotHelper::isSubvolume(bool image, std::string clientname, std::string name)
{
#ifdef _WIN32
	return isSubvolumeWindows(clientname, name);
#else
	int rc=system((helper_name + " "+convert(BackupServer::getSnapshotMethod(image))+" issubvolume \""+(clientname)+"\" \""+(name)+"\"").c_str());
	return rc==0;
#endif
}

void SnapshotHelper::setSnapshotHelperCommand(std::string helper_command)
{
	helper_name=helper_command;
}

bool SnapshotHelper::makeReadonly(bool image, std::string clientname, std::string name)
{
#ifdef _WIN32
	return true;
#else
	int rc = system((helper_name + " " + convert(BackupServer::getSnapshotMethod(image)) + " makereadonly \"" + clientname + "\" \"" + name + "\"").c_str());
	return rc == 0;
#endif
}

std::string SnapshotHelper::getMountpoint(bool image, std::string clientname, std::string name)
{
	std::string ret;
	int rc = os_popen(helper_name + " " + convert(BackupServer::getSnapshotMethod(image)) + " mountpoint \"" + clientname + "\" \"" + name + "\"",
		ret);

	if (rc != 0)
	{
		return std::string();
	}

	return trim(ret);
}

#ifdef _WIN32

namespace
{
	typedef struct {
		BOOL readonly;
		BOOL posix;
		USHORT namelen;
		WCHAR name[1];
	} btrfs_create_subvol;

	typedef struct {
		HANDLE subvol;
		BOOL readonly;
		BOOL posix;
		uint16_t namelen;
		WCHAR name[1];
	} btrfs_create_snapshot;

	typedef struct {
		uint64_t subvol;
		uint64_t inode;
		BOOL top;
	} btrfs_get_file_ids;
}

#define FSCTL_BTRFS_GET_FILE_IDS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x829, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define FSCTL_BTRFS_CREATE_SUBVOL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82a, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define FSCTL_BTRFS_CREATE_SNAPSHOT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82b, METHOD_IN_DIRECT, FILE_ANY_ACCESS)

typedef NTSTATUS(__stdcall* NtFsControlFilePtr)(
	HANDLE FileHandle,
	HANDLE Event,
	PVOID ApcRoutine,
	PVOID ApcContext,
	PIO_STATUS_BLOCK IoStatusBlock,
	ULONG FsControlCode,
	PVOID InputBuffer,
	ULONG InputBufferLength,
	PVOID OutputBuffer,
	ULONG OutputBufferLength);

NtFsControlFilePtr NtFsControlFilePtr_fun;

std::string SnapshotHelper::getBackupStoragePath()
{
	IDatabase* db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	if (db == nullptr)
		return std::string();

	ServerSettings settings(db);
	return settings.getSettings()->backupfolder;
}

bool SnapshotHelper::createEmptyFilesystemWindows(std::string clientname, std::string name, std::string& errmsg)
{
	std::string backup_storage_path = getBackupStoragePath();
	if (backup_storage_path.empty())
		return false;

	std::string parent_path = backup_storage_path + os_file_sep() + clientname;

	std::string subvol_path = parent_path + os_file_sep() + name;

	if (os_get_file_type(subvol_path) != 0)
	{
		errmsg = subvol_path + " already exists";
		return false;
	}

	HANDLE h = CreateFileW(Server->ConvertToWchar(parent_path).c_str(),
		FILE_ADD_SUBDIRECTORY, FILE_SHARE_READ| FILE_SHARE_WRITE|FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

	if (h == INVALID_HANDLE_VALUE)
	{
		errmsg = "Failed to open " + parent_path + ". " + os_last_error_str();
		return false;
	}

	std::wstring name_w = Server->ConvertToWchar(name);

	DWORD csb_len = static_cast<DWORD>(offsetof(btrfs_create_subvol, name[0]) + name_w.size()*sizeof(WCHAR));
	std::vector<char> create_subvol_buf(csb_len);
	btrfs_create_subvol* create_subvol = reinterpret_cast<btrfs_create_subvol*>(create_subvol_buf.data());
	create_subvol->namelen = static_cast<USHORT>(name_w.size() * sizeof(WCHAR));
	memcpy(create_subvol->name, name_w.data(), create_subvol->namelen);

	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtFsControlFilePtr_fun(h, nullptr, nullptr, nullptr, &iosb,
		FSCTL_BTRFS_CREATE_SUBVOL, create_subvol, csb_len, nullptr, 0);

	CloseHandle(h);

	if (!NT_SUCCESS(status))
	{
		errmsg = "FSCTL_BTRFS_CREATE_SUBVOL failed. Status="+convert(status)+" - " + os_last_error_str();
		return false;
	}

	return true;
}

bool SnapshotHelper::removeFilesystemWindows(std::string clientname, std::string name)
{
	std::string backup_storage_path = getBackupStoragePath();
	if (backup_storage_path.empty())
		return false;

	std::string parent_path = backup_storage_path + os_file_sep() + clientname;

	std::string subvol_path = parent_path + os_file_sep() + name;

	return os_remove_nonempty_dir(os_file_prefix(subvol_path));
}

int SnapshotHelper::testWindows()
{
	Server->Log("Testing for winbtrfs...", LL_INFO);
	if (NtFsControlFilePtr_fun == nullptr)
		return -1;

	std::string backup_storage_path = getBackupStoragePath();
	if (backup_storage_path.empty())
		return -1;

	std::string clientname = "testA54hj5luZtlorr494";
	std::string clientdir = backup_storage_path + os_file_sep()
		+ clientname;


	bool create_dir_rc = os_create_dir(clientdir);
	if (!create_dir_rc)
	{
		removeFilesystem(false, clientname, "A");
		removeFilesystem(false, clientname, "B");
		os_remove_dir(clientdir);
	}

	create_dir_rc = create_dir_rc || os_create_dir(clientdir);

	if (!create_dir_rc)
	{
		Server->Log("Btrfs test failed. Could not clean and re-create client dir", LL_INFO);
		return -1;
	}

	std::string errmsg;
	if (!createEmptyFilesystem(false, clientname, "A", errmsg))
	{
		os_remove_dir(clientdir);
		Server->Log("Btrfs test failed. Creating btrfs subvol failed: " + errmsg, LL_INFO);
		return -1;
	}

	writestring("test2", clientdir + os_file_sep() + "A" + os_file_sep() + "test2");

	bool suc = true;

	if (!snapshotFileSystemWindows(clientname, "A", "B", errmsg))
	{
		Server->Log("Btrfs test failed. Snapshotting btrfs subvol failed: " + errmsg, LL_INFO);
		suc = false;
	}

	if (suc)
	{
		writestring("test", clientdir + os_file_sep() + "A" + os_file_sep() + "test");

		if (!os_create_hardlink(clientdir + os_file_sep() + "B" + os_file_sep() + "test", clientdir + os_file_sep() + "A" + os_file_sep() + "test", true, NULL))
		{
			Server->Log("Btrfs test failed. Reflinking file failed." + os_last_error_str(), LL_INFO);
			suc = false;
		}
		else
		{
			if (getFile(clientdir + os_file_sep() + "B" + os_file_sep() + "test") != "test")
			{
				Server->Log("Btrfs test failed. File 1 has wrong contents", LL_ERROR);
				suc = false;
			}

			if (getFile(clientdir + os_file_sep() + "B" + os_file_sep() + "test2") != "test2")
			{
				suc = false;
				Server->Log("Btrfs test failed. File 2 has wrong contents", LL_ERROR);
			}
		}
	}

	if (!removeFilesystemWindows(clientname, "A"))
	{
		Server->Log("Btrfs test failed. Could not remove subvolume A", LL_INFO);
		suc = false;
	}

	if (!removeFilesystemWindows(clientname, "B"))
	{
		Server->Log("Btrfs test failed. Could not remove subvolume A", LL_INFO);
		suc = false;
	}

	if (!os_remove_dir(clientdir))
	{
		Server->Log("Btrfs test failed. Could not remove client dir", LL_INFO);
		return -1;
	}

	if (!suc)
	{
		return -1;
	}

	Server->Log("Winbtrfs present and okay", LL_INFO);

	return 0;
}

void SnapshotHelper::setupWindows()
{
	HMODULE mod = GetModuleHandleW(L"ntdll.dll");
	if (mod == nullptr)
		return;

	NtFsControlFilePtr_fun = reinterpret_cast<NtFsControlFilePtr>(GetProcAddress(mod,
		"NtFsControlFile"));
}

bool SnapshotHelper::snapshotFileSystemWindows(std::string clientname, std::string old_name, std::string snapshot_name, std::string& errmsg)
{
	std::string backup_storage_path = getBackupStoragePath();
	if (backup_storage_path.empty())
		return false;

	std::string parent_path = backup_storage_path + os_file_sep() + clientname;

	std::string old_subvol_path = parent_path + os_file_sep() + old_name;

	if (os_get_file_type(old_subvol_path) == 0)
	{
		errmsg = "Snapshot parent "+ old_subvol_path + " does not exist";
		return false;
	}

	HANDLE h = CreateFileW(Server->ConvertToWchar(parent_path).c_str(),
		FILE_ADD_SUBDIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

	if (h == INVALID_HANDLE_VALUE)
	{
		errmsg = "Failed to open new subvol parent " + parent_path + ". " + os_last_error_str();
		return false;
	}

	HANDLE h_old = CreateFileW(Server->ConvertToWchar(old_subvol_path).c_str(),
		FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

	if (h_old == INVALID_HANDLE_VALUE)
	{
		errmsg = "Failed to open old subvol " + old_subvol_path + ". " + os_last_error_str();
		CloseHandle(h);
		return false;
	}

	std::wstring name_w = Server->ConvertToWchar(snapshot_name);

	DWORD csb_len = static_cast<DWORD>(offsetof(btrfs_create_snapshot, name[0]) + name_w.size() * sizeof(WCHAR));
	std::vector<char> create_snabshot_buf(csb_len);
	btrfs_create_snapshot* create_snapshot = reinterpret_cast<btrfs_create_snapshot*>(create_snabshot_buf.data());
	create_snapshot->namelen = static_cast<USHORT>(name_w.size() * sizeof(WCHAR));
	memcpy(create_snapshot->name, name_w.data(), create_snapshot->namelen);
	create_snapshot->subvol = h_old;

	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtFsControlFilePtr_fun(h, nullptr, nullptr, nullptr, &iosb,
		FSCTL_BTRFS_CREATE_SNAPSHOT, create_snapshot, csb_len, nullptr, 0);

	CloseHandle(h);
	CloseHandle(h_old);

	if (!NT_SUCCESS(status))
	{
		errmsg = "FSCTL_BTRFS_CREATE_SNAPSHOT failed. Status=" + convert(status) + " - " + os_last_error_str();
		return false;
	}

	return true;
}

bool SnapshotHelper::isSubvolumeWindows(std::string clientname, std::string name)
{
	std::string backup_storage_path = getBackupStoragePath();
	if (backup_storage_path.empty())
		return false;

	std::string parent_path = backup_storage_path + os_file_sep() + clientname;

	std::string subvol_path = parent_path + os_file_sep() + name;

	if (os_get_file_type(subvol_path) == 0)
		return false;

	HANDLE h = CreateFileW(Server->ConvertToWchar(subvol_path).c_str(),
		FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

	if (h == INVALID_HANDLE_VALUE)
		return false;

	btrfs_get_file_ids file_ids;

	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtFsControlFilePtr_fun(h, nullptr, nullptr, nullptr, &iosb,
		FSCTL_BTRFS_GET_FILE_IDS, nullptr, 0, &file_ids, sizeof(file_ids));

	CloseHandle(h);

	if (!NT_SUCCESS(status))
		return false;

	return file_ids.inode == 0x100;
}

#endif //_WIN32
