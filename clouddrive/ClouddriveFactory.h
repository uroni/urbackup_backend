#pragma once

#include "IClouddriveFactory.h"

class ClouddriveFactory : public IClouddriveFactory
{
public:
	virtual bool checkConnectivity(CloudSettings settings, int64 timeoutms) override;
	virtual IKvStoreBackend* createBackend(IBackupFileSystem* cachefs, CloudSettings settings) override;
	virtual IFile* createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings) override;
	virtual bool setTopFs(IFile* cloudFile, IBackupFileSystem* fs) override;
	virtual bool runBackgroundWorker(IFile* cloudfile, const std::string& output_fn) override;
	virtual bool isCloudFile(IFile* cloudfile) override;
	virtual int64 getCfTransid(IFile* cloudfile) override;
	virtual bool flush(IFile* cloudfile, bool do_submit) override;
	virtual std::string getCfNumDirtyItems(IFile* cloudfile) override;

private:
	IFile* createCloudFile(IBackupFileSystem* cachefs, CloudSettings settings, bool check_only);
	IKvStoreBackend* createBackend(IBackupFileSystem* cachefs, const std::string& aes_key, CloudSettings settings);
};