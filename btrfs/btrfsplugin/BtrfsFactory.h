#pragma once

#include "IBtrfsFactory.h"

class BtrfsFactory : public IBtrfsFactory
{
public:

	virtual IBackupFileSystem* openBtrfsImage(const std::string& path) override;
};