/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "PipeSessions.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "FileServ.h"
#include "FileMetadataPipe.h"
#include "../common/data.h"
#include "PipeFile.h"
#include <string>

volatile bool PipeSessions::do_stop = false;
IMutex* PipeSessions::mutex = NULL;
std::map<std::wstring, SPipeSession> PipeSessions::pipe_files;
std::map<std::wstring, SExitInformation> PipeSessions::exit_information;
std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*> PipeSessions::metadata_callbacks;


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
		std::wstring script_cmd = getuntil(L"|", cmd);

		if(script_cmd==L"urbackup/FILE_METADATA")
		{
			std::wstring server_token = getafter(L"|", cmd);

			IPipe* cmd_pipe = Server->createMemoryPipe();
			FileMetadataPipe* nf = new FileMetadataPipe(cmd_pipe, cmd);

			SPipeSession new_session = {
				nf, false, cmd_pipe
			};

			pipe_files[cmd] = new_session;

			return nf;
		}
		else
		{
			int backupnum = watoi(getuntil(L"|", getafter(L"|", cmd)));

			std::wstring output_filename = ExtractFileName(script_cmd);

			script_cmd.erase(script_cmd.size()-output_filename.size(), output_filename.size());

			std::wstring script_filename = FileServ::mapScriptOutputNameToScript(output_filename);

			script_cmd = L"\"" + script_cmd + script_filename +
				L"\" \"" + output_filename + L"\" "+convert(backupnum);

			PipeFile* nf = new PipeFile(script_cmd);
			if(nf->getHasError())
			{
				delete nf;
				return NULL;
			}

			SPipeSession new_session = {
				nf, false, NULL
			};
			pipe_files[cmd] = new_session;

			return nf;
		}		
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
		delete it->second.input_pipe;
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
				delete it->second.input_pipe;
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

void PipeSessions::transmitFileMetadata( const std::string& local_fn, const std::string& public_fn,
	const std::string& server_token, const std::string& identity )
{
	if(public_fn.empty() || next(public_fn, 0, "urbackup/"))
	{
		return;
	}

	std::string sharename = getuntil("/", public_fn);

	CWData data;
	data.addString(public_fn);
	data.addString(local_fn);

	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter_cb = 
		metadata_callbacks.find(std::make_pair(sharename, identity));

	if(iter_cb!=metadata_callbacks.end())
	{
		data.addVoidPtr(iter_cb->second);
	}

	std::map<std::wstring, SPipeSession>::iterator it = pipe_files.find(widen("urbackup/FILE_METADATA|"+server_token));

	if(it!=pipe_files.end() && it->second.input_pipe!=NULL)
	{
		it->second.input_pipe->Write(data.getDataPtr(), data.getDataSize());
	}
}

void PipeSessions::metadataStreamEnd( const std::string& server_token )
{
	IScopedLock lock(mutex);

	std::map<std::wstring, SPipeSession>::iterator it = pipe_files.find(widen("urbackup/FILE_METADATA|"+server_token));

	if(it!=pipe_files.end() && it->second.input_pipe!=NULL)
	{
		CWData data;
		data.addString(std::string());
		data.addString(std::string());

		it->second.input_pipe->Write(data.getDataPtr(), data.getDataSize());
	}
}

void PipeSessions::registerMetadataCallback( const std::wstring &name, const std::string& identity, IFileServ::IMetadataCallback* callback )
{
	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter
		= metadata_callbacks.find(std::make_pair(Server->ConvertToUTF8(name), identity));

	if(iter!=metadata_callbacks.end())
	{
		delete iter->second;
	}

	metadata_callbacks[std::make_pair(Server->ConvertToUTF8(name), identity)] = callback;
}

void PipeSessions::removeMetadataCallback( const std::wstring &name, const std::string& identity )
{
	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter
		= metadata_callbacks.find(std::make_pair(Server->ConvertToUTF8(name), identity));

	if(iter!=metadata_callbacks.end())
	{
		delete iter->second;
	}
}

