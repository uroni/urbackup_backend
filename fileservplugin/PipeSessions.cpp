#include "PipeSessions.h"
#include "../stringtools.h"

volatile bool PipeSessions::do_stop = false;
IMutex* PipeSessions::mutex = NULL;
std::map<std::wstring, SPipeSession> PipeSessions::pipe_files;

IFile* PipeSessions::getFile(const std::wstring& cmd)
{
	IScopedLock lock(mutex);

	std::map<std::wstring, SPipeSession>::iterator it = pipe_files.find(cmd);
	if(it != pipe_files.end())
	{
		return it->second.file;
	}
	else
	{
		PipeFile* nf = new PipeFile(getuntil(L"|", cmd));
		if(nf->getHasError())
		{
			delete nf;
			return NULL;
		}

		SPipeSession new_session = {
			nf
		};
		pipe_files[cmd] = new_session;

		return nf;
	}
}

void PipeSessions::init()
{
	mutex = Server->createMutex();
}

void PipeSessions::destroy()
{
	delete mutex;
}

void PipeSessions::removeFile(const std::wstring& cmd)
{
	IScopedLock lock(mutex);

	std::map<std::wstring, SPipeSession>::iterator it = pipe_files.find(cmd);
	if(it != pipe_files.end())
	{
		delete it->second.file;
		pipe_files.erase(it);
	}
}

