#pragma once
#include "../urbackupcommon/fileclient/FileClient.h"
#include "../Interface/Thread.h"
#include "server_prepare_hash.h"
#include "../urbackupcommon/chunk_hasher.h"
#include "server_log.h"
#include <memory>

class FileMetadataDownloadThread : public IThread
{
public:
	FileMetadataDownloadThread(FileClient* fc, const std::string& server_token, logid_t logid);

	virtual void operator()();

	bool applyMetadata(const std::wstring& backup_metadata_dir, const std::wstring& backup_dir, INotEnoughSpaceCallback *cb);
	bool applyWindowsMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset);
    bool applyUnixMetadata(IFile* metadata_f, IFile* output_f, int64& metadata_size, INotEnoughSpaceCallback *cb, int64 output_offset);

private:
	std::auto_ptr<FileClient> fc;
	const std::string& server_token;

	std::vector<char> buffer;

	bool has_error;
	std::wstring metadata_tmp_fn;
	logid_t logid;
};
