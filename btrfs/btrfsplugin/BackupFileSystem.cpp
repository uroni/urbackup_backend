#include "BackupFileSystem.h"

BtrfsBackupFileSystem::BtrfsBackupFileSystem(const std::string& backing_path)
	: btrfs(btrfs_fuse_open_disk_image(backing_path))
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
