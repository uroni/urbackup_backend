#pragma once

#include "../../Interface/Object.h"
#include "../../Interface/File.h"
#include "../../urbackupcommon/os_functions.h"


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
};