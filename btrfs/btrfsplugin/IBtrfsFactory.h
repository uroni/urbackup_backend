#pragma once
#include "../../Interface/BackupFileSystem.h"
#include "../../Interface/Plugin.h"
#include "../../Interface/File.h"

class IBtrfsFactory : public IPlugin
{
public:
	virtual IBackupFileSystem* openBtrfsImage(IFile* img) = 0;

	virtual bool formatVolume(IFile* img) = 0;
};