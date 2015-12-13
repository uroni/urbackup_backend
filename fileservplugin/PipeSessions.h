#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "PipeFileBase.h"
#include <map>
#include <string>
#include "IFileServ.h"

struct SPipeSession
{
	PipeFileBase* file;
	bool retrieved_exit_info;
	IPipe* input_pipe;
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

	static IFile* getFile(const std::string& cmd);
	static void removeFile(const std::string& cmd);
	static SExitInformation getExitInformation(const std::string& cmd);

	static void transmitFileMetadata(const std::string& local_fn, const std::string& public_fn,
		const std::string& server_token, const std::string& identity, int64 folder_items);

	static void metadataStreamEnd(const std::string& server_token);

	static void registerMetadataCallback(const std::string &name, const std::string& identity, IFileServ::IMetadataCallback* callback);
	static void removeMetadataCallback(const std::string &name, const std::string& identity);

	void operator()();

private:
	static IMutex* mutex;
	static volatile bool do_stop;
	static std::map<std::string, SPipeSession> pipe_files;
	static std::map<std::string, SExitInformation> exit_information;
	static std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*> metadata_callbacks;
};