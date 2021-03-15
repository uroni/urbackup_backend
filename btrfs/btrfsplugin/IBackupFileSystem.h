#pragma once

#include "../../Interface/Object.h"
#include "../../Interface/File.h"
#include "../../urbackupcommon/os_functions.h"
#include "../fuse/fuse_t.h"


class IBackupFileSystem : public IObject
{
public:
	virtual bool hasError() = 0;

	virtual IFsFile* openFile(const std::string& path, int mode) = 0;

	virtual bool reflinkFile(const std::string& source, const std::string& dest) = 0;

	virtual bool createDir(const std::string& path) = 0;

	virtual bool deleteFile(const std::string& path) = 0;

	virtual EFileType getFileType(const std::string& path) = 0;

	virtual bool Flush() = 0;

	virtual std::vector<SBtrfsFile> listFiles(const std::string& path) = 0;

	virtual bool createSubvol(const std::string& path) = 0;

	virtual bool createSnapshot(const std::string& src_path,
		const std::string& dest_path) = 0;

	virtual bool rename(const std::string& src_name,
		const std::string& dest_name) = 0;
};