#pragma once
#include "../Interface/File.h"
#include "PipeFile.h"
#include <map>

class PipeSessions
{
public:
	static void init();
	static void destroy();

	static IFile* getFile(const std::wstring& cmd);
	static void removeFile(const std::wstring& cmd);

	void operator()();

private:
	static IMutex* mutex;
	static volatile bool do_stop;
	static std::map<std::wstring, PipeFile*> pipe_files;
};