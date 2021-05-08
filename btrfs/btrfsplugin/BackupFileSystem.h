#pragma once

#include "IBackupFileSystem.h"
#include "../fuse/fuse.h"

class BtrfsBackupFileSystem : public IBackupFileSystem
{
public:
	BtrfsBackupFileSystem(IFile* img);
	virtual IFsFile* openFile(const std::string& path, int mode) override;
	virtual bool reflinkFile(const std::string& source, const std::string& dest) override;
	virtual bool createDir(const std::string& path) override;
	virtual EFileType getFileType(const std::string& path) override;
	virtual bool hasError() override;
	virtual bool Flush() override;
	virtual bool deleteFile(const std::string& path) override;
	virtual std::vector<SBtrfsFile> listFiles(const std::string& path) override;
	virtual bool createSubvol(const std::string& path) override;
	virtual bool createSnapshot(const std::string& src_path, const std::string& dest_path) override;
	virtual bool rename(const std::string& src_name, const std::string& dest_name) override;
	virtual bool removeDirRecursive(const std::string& path) override;
	virtual bool directoryExists(const std::string& path) override;
	virtual bool linkSymbolic(const std::string& target, const std::string& lname) override;
	virtual bool copyFile(const std::string& src, const std::string& dst, bool flush, std::string* error_str) override;

private:
	std::unique_ptr<BtrfsFuse> btrfs;
};