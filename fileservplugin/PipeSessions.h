#pragma once
#include "../Interface/File.h"
#include "../Interface/Thread.h"
#include "PipeFile.h"
#include <map>
#include <string>

struct SPipeSession
{
	PipeFile* file;
	bool retrieved_exit_info;
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

	void operator()();

private:
	static IMutex* mutex;
	static volatile bool do_stop;
	static std::map<std::wstring, SPipeSession> pipe_files;
	static std::map<std::wstring, SExitInformation> exit_information;
};