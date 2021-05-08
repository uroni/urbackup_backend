/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2021 Martin Raiber
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

#include "BackupFileSystem.h"

BtrfsBackupFileSystem::BtrfsBackupFileSystem(IFile* img)
	: btrfs(btrfs_fuse_open_disk_image(img))
{

}

IFsFile* BtrfsBackupFileSystem::openFile(const std::string& path, int mode)
{
	return btrfs->openFile(path, mode);
}

bool BtrfsBackupFileSystem::reflinkFile(const std::string& source, const std::string& dest)
{
	return btrfs->reflink(source, dest);
}

bool BtrfsBackupFileSystem::createDir(const std::string& path)
{
	return btrfs->createDir(path);
}

EFileType BtrfsBackupFileSystem::getFileType(const std::string& path)
{
	int ft = static_cast<unsigned int>(btrfs->getFileType(path));
	int ret = 0;
	if (ft & static_cast<unsigned int>(BtrfsFuse::FileType::Directory))
	{
		ret |= EFileType_Directory;
	}
	else if (ft & static_cast<unsigned int>(BtrfsFuse::FileType::File))
	{
		ret |= EFileType_File;
	}

	return static_cast<EFileType>(ret);
}

bool BtrfsBackupFileSystem::hasError()
{
	return btrfs.get()==nullptr;
}

bool BtrfsBackupFileSystem::Flush()
{
	return btrfs->flush();
}

bool BtrfsBackupFileSystem::deleteFile(const std::string& path)
{
	return btrfs->deleteFile(path);
}

std::vector<SBtrfsFile> BtrfsBackupFileSystem::listFiles(const std::string& path)
{
	return btrfs->listFiles(path);
}

bool BtrfsBackupFileSystem::createSubvol(const std::string& path)
{
	return btrfs->create_subvol(path);
}

bool BtrfsBackupFileSystem::createSnapshot(const std::string& src_path, const std::string& dest_path)
{
	return btrfs->create_snapshot(src_path, dest_path);
}

bool BtrfsBackupFileSystem::rename(const std::string& src_name, const std::string& dest_name)
{
	return btrfs->rename(src_name, dest_name);
}

bool BtrfsBackupFileSystem::removeDirRecursive(const std::string& path)
{
	std::vector<SBtrfsFile> files = btrfs->listFiles(path);

	for (SBtrfsFile& file : files)
	{
		if (file.isdir)
		{
			if (!removeDirRecursive(path + "\\" + file.name))
			{
				return false;
			}
		}
		else
		{
			if (!deleteFile(path + "\\" + file.name))
			{
				return false;
			}
		}
	}
	return deleteFile(path);
}

bool BtrfsBackupFileSystem::directoryExists(const std::string& path)
{
	return (getFileType(path) & EFileType_Directory)>0;
}

bool BtrfsBackupFileSystem::linkSymbolic(const std::string& target, const std::string& lname)
{
	return false;
}

bool BtrfsBackupFileSystem::copyFile(const std::string& src, const std::string& dst, bool flush, std::string* error_str)
{
	std::unique_ptr<IFile> fsrc(openFile(src, MODE_READ));
	if (fsrc.get()==nullptr)
	{
		return false;
	}
	std::unique_ptr<IFile> fdst(openFile(dst, MODE_WRITE));
	if (fdst.get() == nullptr)
	{
		return false;
	}

	if (!fsrc->Seek(0))
	{
		return false;
	}

	if (!fdst->Seek(0))
	{
		return false;
	}

	std::vector<char> buf(512 * 1024);
	size_t rc;
	bool has_error = false;
	while ((rc = (_u32)fsrc->Read(buf.data(), buf.size(), &has_error)) > 0)
	{
		if (has_error)
		{
			break;
		}

		if (rc > 0)
		{
			fdst->Write(buf.data(), (_u32)rc, &has_error);

			if (has_error)
			{
				break;
			}
		}
	}

	if (has_error)
	{
		return false;
	}
	else
	{
		return true;
	}

	if (!has_error && flush)
	{
		has_error = !fdst->Sync();
	}

	return !has_error;
}
