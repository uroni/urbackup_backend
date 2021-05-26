#pragma once

#include "../../Interface/BackupFileSystem.h"
#include "../fuse/fuse.h"

class BtrfsBackupFileSystem : public IBackupFileSystem
{
public:
	BtrfsBackupFileSystem(IFile* img);
	virtual IFsFile* openFile(const std::string& path, int mode) override;
	virtual bool reflinkFile(const std::string& source, const std::string& dest) override;
	virtual bool createDir(const std::string& path) override;
	virtual int getFileType(const std::string& path) override;
	virtual bool hasError() override;
	virtual bool deleteFile(const std::string& path) override;
	virtual std::vector<SFile> listFiles(const std::string& path) override;
	virtual bool createSubvol(const std::string& path) override;
	virtual bool createSnapshot(const std::string& src_path, const std::string& dest_path) override;
	virtual bool rename(const std::string& src_name, const std::string& dest_name) override;
	virtual bool removeDirRecursive(const std::string& path) override;
	virtual bool directoryExists(const std::string& path) override;
	virtual bool linkSymbolic(const std::string& target, const std::string& lname) override;
	virtual bool copyFile(const std::string& src, const std::string& dst, bool flush, std::string* error_str) override;
	virtual bool sync(const std::string& path) override;
	virtual bool deleteSubvol(const std::string& path) override;
	virtual int64 totalSpace() override;
	virtual int64 freeSpace() override;
	virtual int64 freeMetadataSpace() override;
	virtual int64 unallocatedSpace() override;
	virtual std::string fileSep() override;
	virtual std::string filePath(IFile* f) override;
	virtual bool getXAttr(const std::string& path, const std::string& key, std::string& value) override;
	virtual bool setXAttr(const std::string& path, const std::string& key, const std::string& val) override;
	virtual std::string getName() override;
	virtual bool forceAllocMetadata() override;
	virtual bool balance(int usage, size_t limit, bool metadata, bool& enospc, size_t& relocated) override;
	virtual IFile* getBackingFile() override;
	virtual std::string lastError() override;
	virtual std::vector<SChunk> getChunks() override;
private:
	BtrfsFuse btrfs;
	IFile* backing_file;	
};