#pragma once
#include "fileclient/FileClient.h"
#include "../Interface/Thread.h"
#include "server_prepare_hash.h"

class FileMetadataDownloadThread : public IThread
{
public:
	FileMetadataDownloadThread(FileClient& fc, const std::string& server_token, int clientid);

	virtual void operator()();

	bool applyMetadata(const std::wstring& backupdir, INotEnoughSpaceCallback *cb);
	bool applyWindowsMetadata(IFile* metadata_f, IFile* output_f, INotEnoughSpaceCallback *cb);

private:
	FileClient& fc;
	const std::string& server_token;
	int clientid;

	std::vector<char> buffer;

	bool has_error;
	std::wstring metadata_tmp_fn;
};