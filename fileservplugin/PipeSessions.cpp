#include "PipeSessions.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"

volatile bool PipeSessions::do_stop = false;
IMutex* PipeSessions::mutex = NULL;
std::map<std::wstring, SPipeSession> PipeSessions::pipe_files;
std::map<std::wstring, SExitInformation> PipeSessions::exit_information;

const int64 pipe_file_timeout = 1*60*60*1000;


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
			nf, false
		};
		pipe_files[cmd] = new_session;

		return nf;
	}
}

void PipeSessions::init()
{
	mutex = Server->createMutex();

	Server->getThreadPool()->execute(new PipeSessions);
}

void PipeSessions::destroy()
{
	delete mutex;

	do_stop=true;
}

void PipeSessions::removeFile(const std::wstring& cmd)
{
	IScopedLock lock(mutex);

	std::map<std::wstring, SPipeSession>::iterator it = pipe_files.find(cmd);
	if(it != pipe_files.end())
	{
		if(!it->second.retrieved_exit_info)
		{
			int exit_code = -1;
			it->second.file->getExitCode(exit_code);
			SExitInformation exit_info(
				exit_code,
				it->second.file->getStdErr(),
				Server->getTimeMS() );

			exit_information[cmd] = exit_info;
		}		

		delete it->second.file;
		pipe_files.erase(it);
	}
}

SExitInformation PipeSessions::getExitInformation(const std::wstring& cmd)
{
	IScopedLock lock(mutex);

	{
		std::map<std::wstring, SExitInformation>::iterator it=exit_information.find(cmd);

		if(it!=exit_information.end())
		{
			SExitInformation ret = it->second;
			exit_information.erase(it);
			return ret;
		}
	}

	{
		std::map<std::wstring, SPipeSession>::iterator it = pipe_files.find(cmd);
		if(it != pipe_files.end() && !it->second.retrieved_exit_info)
		{
			int exit_code = -1;
			it->second.file->getExitCode(exit_code);
			SExitInformation exit_info(
				exit_code,
				it->second.file->getStdErr(),
				Server->getTimeMS() );

			it->second.retrieved_exit_info=true;

			return exit_info;
		}
	}

	return SExitInformation();
}

void PipeSessions::operator()()
{
	bool leak_check = Server->getServerParameter("leak_check")=="true";

	unsigned int waittime = leak_check ? 1000 : 60000;


	while(!do_stop)
	{
		Server->wait(waittime);

		IScopedLock lock(mutex);

		int64 currtime = Server->getTimeMS();

		for(std::map<std::wstring, SPipeSession>::iterator it=pipe_files.begin();
			it!=pipe_files.end();)
		{
			if(currtime - it->second.file->getLastRead() > pipe_file_timeout)
			{
				delete it->second.file;
				std::map<std::wstring, SPipeSession>::iterator del_it = it;
				++it;
				pipe_files.erase(del_it);
			}
			else
			{
				++it;
			}
		}

		for(std::map<std::wstring, SExitInformation>::iterator it=exit_information.begin();
			it!=exit_information.end();)
		{
			if(currtime - it->second.created > pipe_file_timeout)
			{
				std::map<std::wstring, SExitInformation>::iterator del_it = it;
				++it;
				exit_information.erase(del_it);
			}
			else
			{
				++it;
			}
		}
	}
}

