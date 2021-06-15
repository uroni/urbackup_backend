#pragma once

#include "IBtrfsFactory.h"

class BtrfsFactory : public IBtrfsFactory
{
public:
	virtual IBackupFileSystem* openBtrfsImage(IFile* img) override;
	virtual bool formatVolume(IFile* img) override;
};