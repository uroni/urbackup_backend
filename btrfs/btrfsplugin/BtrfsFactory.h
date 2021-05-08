#pragma once

#include "IBtrfsFactory.h"

class BtrfsFactory : public IBtrfsFactory
{
public:
	virtual IBackupFileSystem* openBtrfsImage(IFile* img) override;
};