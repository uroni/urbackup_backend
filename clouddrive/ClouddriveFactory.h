#pragma once

#include "IClouddriveFactory.h"

class ClouddriveFactory : public IClouddriveFactory
{
public:
	virtual bool checkConnectivity(CloudSettings settings, int64 timeoutms) override;
	virtual IFile* createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings) override;

private:
	IFile* createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings, bool check_only);
};