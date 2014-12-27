#include "PipeSessions.h"
#include "../stringtools.h"

volatile bool PipeSessions::do_stop = false;
IMutex* PipeSessions::mutex = NULL;
std::map<std::wstring, PipeFile*> PipeSessions::pipe_files;

IFile* PipeSessions::getFile(const std::wstring& cmd)
{
	IScopedLock lock(mutex);

	std::map<std::wstring, PipeFile*>::iterator it = pipe_files.find(cmd);
	if(it != pipe_files.end())
	{
		return it->second;
	}
	else
	{
		PipeFile* nf = new PipeFile(getuntil(L"|", cmd));
		if(nf->getHasError())
		{
			delete nf;
			return NULL;
		}

		pipe_files[cmd] = nf;
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

	std::map<std::wstring, PipeFile*>::iterator it = pipe_files.find(cmd);
	if(it != pipe_files.end())
	{
		delete it->second;
		pipe_files.erase(it);
	}
}

