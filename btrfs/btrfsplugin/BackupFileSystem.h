#pragma once

#include "IBackupFileSystem.h"
#include "../fuse/fuse.h"

class BtrfsBackupFileSystem : public IBackupFileSystem
{
public:
	BtrfsBackupFileSystem(const std::string& backing_path);
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

private:
	std::unique_ptr<BtrfsFuse> btrfs;
};