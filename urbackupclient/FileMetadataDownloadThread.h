#pragma once
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/Thread.h"
#include "RestoreFiles.h"

class FileMetadataDownloadThread : public IThread
{
public:
	FileMetadataDownloadThread(RestoreFiles& restore, FileClient& fc, const std::string& client_token);

	virtual void operator()();

	bool applyMetadata(const std::wstring& backupdir);
	bool applyWindowsMetadata(IFile* metadata_f, const std::wstring& output_fn);

private:
	RestoreFiles& restore;
	FileClient& fc;
	const std::string& client_token;

	std::vector<char> buffer;

	bool has_error;
	std::wstring metadata_tmp_fn;
};