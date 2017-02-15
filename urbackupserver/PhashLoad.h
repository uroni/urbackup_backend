#pragma once

#include "../Interface/Thread.h"
#include "server_log.h"
#include "../urbackupcommon/fileclient/FileClient.h"

class PhashLoad : public IThread
{
public:
	PhashLoad(FileClient* fc,
		logid_t logid,
		std::string async_id);

	~PhashLoad();

	void operator()();

	bool getHash(int64 file_id, std::string& hash);

	bool hasError();

	void shutdown();

	bool isDownloading();

	void setProgressLogEnabled(bool b);

	bool hasTimeoutError() {
		return has_timeout_error;
	}

private:
	bool has_error;
	bool has_timeout_error;
	bool eof;
	FileClient* fc;
	logid_t logid;
	std::string async_id;
	IFile* phash_file;
	int64 phash_file_pos;
	FileClient::ProgressLogCallback* orig_progress_log_callback;
};
