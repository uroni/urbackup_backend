#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "PipeFileBase.h"
#include <map>
#include <string>
#include "IFileServ.h"

struct SPipeSession
{
	IPipeFile* file;
	bool retrieved_exit_info;
	IPipe* input_pipe;
	int backupnum;
	std::string metadata;
};

struct SExitInformation
{
	SExitInformation()
		: rc(-1), created(0) {}

	SExitInformation(int rc, std::string outdata, int64 created)
		: rc(rc), outdata(outdata), created(created)
	{

	}

	int rc;
	std::string outdata;
	int64 created;
};

class PipeSessions : public IThread
{
public:
	static void init();
	static void destroy();

	static IFile* getFile(const std::string& cmd, ScopedPipeFileUser& pipe_file_user, const std::string& server_token, const std::string& ident);
	static void injectPipeSession(const std::string& session_key, int backupnum, IPipeFile* pipe_file, const std::string& metadata);
	static void removeFile(const std::string& cmd);
	static SExitInformation getExitInformation(const std::string& cmd);

	static void transmitFileMetadata(const std::string& local_fn, const std::string& public_fn,
		const std::string& server_token, const std::string& identity, int64 folder_items, int64 metadata_id);

	static void transmitFileMetadata(const std::string& public_fn, const std::string& metadata,
		const std::string& server_token, const std::string& identity);

	static void fileMetadataDone(const std::string& public_fn, const std::string& server_token);

	static bool isShareActive(const std::string& sharename, const std::string& server_token);

	static void metadataStreamEnd(const std::string& server_token);

	static void registerMetadataCallback(const std::string &name, const std::string& identity, IFileServ::IMetadataCallback* callback);
	static void removeMetadataCallback(const std::string &name, const std::string& identity);

	void operator()();

private:

	static std::string getKey(const std::string& cmd, int& backupnum);

	static IMutex* mutex;
	static volatile bool do_stop;
	static std::map<std::string, SPipeSession> pipe_files;
	static std::map<std::string, SExitInformation> exit_information;
	static std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*> metadata_callbacks;
	static std::map<std::string, size_t> active_shares;
};