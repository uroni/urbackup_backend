#pragma once
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/Thread.h"
#include "RestoreFiles.h"

namespace client {

class FileMetadataDownloadThread : public IThread
{
public:
	FileMetadataDownloadThread(RestoreFiles& restore, FileClient& fc, const std::string& client_token);

	virtual void operator()();

	bool applyMetadata();

	bool applyOsMetadata(IFile* metadata_f, const std::string& output_fn);

	void shutdown();

private:
	RestoreFiles& restore;
	FileClient& fc;
	const std::string& client_token;

	std::vector<char> buffer;

	bool has_error;
	std::string metadata_tmp_fn;
};

} //namespace client
