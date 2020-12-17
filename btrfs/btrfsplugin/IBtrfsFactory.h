#pragma once

#include "IBackupFileSystem.h"
#include "../../Interface/Plugin.h"

class IBtrfsFactory : public IPlugin
{
public:
	virtual IBackupFileSystem* openBtrfsImage(const std::string& path) = 0;
};