#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "PipeFileBase.h"
#include <map>
#include <string>

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

	static IFile* getFile(const std::wstring& cmd);
	static void removeFile(const std::wstring& cmd);
	static SExitInformation getExitInformation(const std::wstring& cmd);

	static void transmitFileMetadata(const std::string& local_fn, const std::string& public_fn,
		const std::string& server_token);

	static void metadataStreamEnd(const std::string& server_token);

	void operator()();

private:
	static IMutex* mutex;
	static volatile bool do_stop;
	static std::map<std::wstring, SPipeSession> pipe_files;
	static std::map<std::wstring, SExitInformation> exit_information;
};