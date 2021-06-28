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
	: btrfs(img), backing_file(img)
{
	if (btrfs.get_has_error())
		return;

	btrfs.resize_max();
}

IFsFile* BtrfsBackupFileSystem::openFile(const std::string& path, int mode)
{
	return btrfs.openFile(path, mode);
}

bool BtrfsBackupFileSystem::reflinkFile(const std::string& source, const std::string& dest)
{
	return btrfs.reflink(source, dest);
}

bool BtrfsBackupFileSystem::createDir(const std::string& path)
{
	return btrfs.createDir(path);
}

int BtrfsBackupFileSystem::getFileType(const std::string& path)
{
	return btrfs.getFileType(path);
}

bool BtrfsBackupFileSystem::hasError()
{
	return btrfs.get_has_error();
}

bool BtrfsBackupFileSystem::deleteFile(const std::string& path)
{
	return btrfs.deleteFile(path);
}

std::vector<SFile> BtrfsBackupFileSystem::listFiles(const std::string& path)
{
	return btrfs.listFiles(path);
}

bool BtrfsBackupFileSystem::createSubvol(const std::string& path)
{
	return btrfs.create_subvol(path);
}

bool BtrfsBackupFileSystem::createSnapshot(const std::string& src_path, const std::string& dest_path)
{
	return btrfs.create_snapshot(src_path, dest_path);
}

bool BtrfsBackupFileSystem::rename(const std::string& src_name, const std::string& dest_name)
{
	return btrfs.rename(src_name, dest_name);
}

bool BtrfsBackupFileSystem::removeDirRecursive(const std::string& path)
{
	std::vector<SFile> files = btrfs.listFiles(path);

	for (SFile& file : files)
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
	while ((rc = (_u32)fsrc->Read(buf.data(), static_cast<_u32>(buf.size()), &has_error)) > 0)
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

bool BtrfsBackupFileSystem::sync(const std::string& path)
{
	return btrfs.flush();
}

bool BtrfsBackupFileSystem::deleteSubvol(const std::string& path)
{
	std::vector<SFile> files = listFiles(path);

	for (SFile& f : files)
	{
		if (f.isdir)
		{
			if (!deleteSubvol(path + "\\" + f.name))
				return false;
		}
		else
		{
			if (!deleteFile(path + "\\" + f.name))
				return false;
		}
	}

	return deleteFile(path);
}

int64 BtrfsBackupFileSystem::totalSpace()
{
	return btrfs.get_total_space();
}

int64 BtrfsBackupFileSystem::freeSpace()
{
	BtrfsFuse::SpaceInfo space_info = btrfs.get_space_info();
	return (space_info.allocated + space_info.unallocated) - space_info.used;
}

int64 BtrfsBackupFileSystem::freeMetadataSpace()
{
	BtrfsFuse::SpaceInfo space_info = btrfs.get_space_info();
	return (space_info.metadata_allocated - space_info.metadata_used) + space_info.unallocated;
}

int64 BtrfsBackupFileSystem::unallocatedSpace()
{
	BtrfsFuse::SpaceInfo space_info = btrfs.get_space_info();
	return space_info.unallocated;
}

std::string BtrfsBackupFileSystem::fileSep()
{
	return "\\";
}

std::string BtrfsBackupFileSystem::filePath(IFile* f)
{
	return f->getFilename();
}

bool BtrfsBackupFileSystem::getXAttr(const std::string& path, const std::string& key, std::string& value)
{
	str_map xattrs = btrfs.get_xattrs(path);
	if (xattrs == str_map())
		return false;

	auto it = xattrs.find(key);
	if (it == xattrs.end())
		return false;

	value = it->second;
	return true;
}

bool BtrfsBackupFileSystem::setXAttr(const std::string& path, const std::string& key, const std::string& val)
{
	return btrfs.set_xattr(path, key, val);
}

std::string BtrfsBackupFileSystem::getName()
{
	return "btrfs:"+backing_file->getFilename();
}

bool BtrfsBackupFileSystem::forceAllocMetadata()
{
	return false;
}

bool BtrfsBackupFileSystem::balance(int usage, size_t limit, bool metadata, bool& enospc, size_t& relocated)
{
	return false;
}

IFile* BtrfsBackupFileSystem::getBackingFile()
{
	return backing_file;
}

std::string BtrfsBackupFileSystem::lastError()
{
	return btrfs.errno_to_str(errno)
		+ " (" + std::to_string(errno) + ")";
}

std::vector<BtrfsBackupFileSystem::SChunk> BtrfsBackupFileSystem::getChunks()
{
	auto res = btrfs.get_chunks();

	std::vector<BtrfsBackupFileSystem::SChunk> ret;
	if(res.first==nullptr)
		return ret;

	ret.assign(reinterpret_cast<SChunk*>(res.first),
		reinterpret_cast<SChunk*>(res.first) + res.second);

	free(res.first);

	return ret;
}
