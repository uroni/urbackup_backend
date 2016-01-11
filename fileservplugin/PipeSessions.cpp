/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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
std::map<std::string, SPipeSession> PipeSessions::pipe_files;
std::map<std::string, SExitInformation> PipeSessions::exit_information;
std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*> PipeSessions::metadata_callbacks;
std::map<std::string, size_t> PipeSessions::active_shares;

const int64 pipe_file_timeout = 1*60*60*1000;


IFile* PipeSessions::getFile(const std::string& cmd, ScopedPipeFileUser& pipe_file_user)
{
	if(cmd.empty())
	{
		return NULL;
	}

	IScopedLock lock(mutex);

	int backupnum;
	std::string session_key = getKey(cmd, backupnum);
	std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);

	if(it!=pipe_files.end() && it->second.backupnum!=backupnum)
	{
		removeFile(cmd);
		it=pipe_files.end();
	}

	if(it != pipe_files.end())
	{
		pipe_file_user.reset(it->second.file);
		return it->second.file;
	}
	else
	{
		std::string script_cmd = getuntil("|", cmd);

		if(script_cmd=="urbackup/FILE_METADATA")
		{
			IPipe* cmd_pipe = Server->createMemoryPipe();
			FileMetadataPipe* nf = new FileMetadataPipe(cmd_pipe, cmd);

			SPipeSession new_session = {
				nf, false, cmd_pipe, backupnum
			};

			pipe_files[session_key] = new_session;

			pipe_file_user.reset(nf);
			return nf;
		}
		else
		{
			std::string output_filename = ExtractFileName(script_cmd);

			script_cmd.erase(script_cmd.size()-output_filename.size(), output_filename.size());

			std::string script_filename = FileServ::mapScriptOutputNameToScript(output_filename);

			script_cmd = "\"" + script_cmd + script_filename +
				"\" \"" + output_filename + "\" "+convert(backupnum);

			PipeFile* nf = new PipeFile(script_cmd);
			if(nf->getHasError())
			{
				delete nf;
				return NULL;
			}

			SPipeSession new_session = {
				nf, false, NULL, backupnum
			};
			pipe_files[session_key] = new_session;

			pipe_file_user.reset(nf);
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

void PipeSessions::removeFile(const std::string& cmd)
{
	if(cmd.empty())
	{
		return;
	}

	IScopedLock lock(mutex);

	int backupnum;
	std::string session_key = getKey(cmd, backupnum);

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);
	if(it != pipe_files.end())
	{
		Server->Log("Removing pipe file "+cmd, LL_DEBUG);

		if(!it->second.retrieved_exit_info)
		{
			int exit_code = -1;
			it->second.file->getExitCode(exit_code);
			SExitInformation exit_info(
				exit_code,
				it->second.file->getStdErr(),
				Server->getTimeMS() );

			exit_information[cmd] = exit_info;

			Server->Log("Pipe file has exit code "+convert(exit_code), LL_DEBUG);
		}		

		it->second.file->forceExitWait();

		while (it->second.file->hasUser())
		{
			Server->wait(100);
		}

		delete it->second.file;
		delete it->second.input_pipe;
		pipe_files.erase(it);
	}
	else
	{
		Server->Log("Pipe file "+cmd+" not found while removing pipe file", LL_INFO);
	}
}

SExitInformation PipeSessions::getExitInformation(const std::string& cmd)
{
	IScopedLock lock(mutex);

	int backupnum;
	std::string session_key = getKey(cmd, backupnum);

	{
		std::map<std::string, SExitInformation>::iterator it=exit_information.find(session_key);

		if(it!=exit_information.end())
		{
			SExitInformation ret = it->second;
			exit_information.erase(it);
			return ret;
		}
	}

	{
		std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);
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

		for(std::map<std::string, SPipeSession>::iterator it=pipe_files.begin();
			it!=pipe_files.end();)
		{
			if(currtime - it->second.file->getLastRead() > pipe_file_timeout)
			{
				it->second.file->forceExitWait();
				delete it->second.file;
				delete it->second.input_pipe;
				std::map<std::string, SPipeSession>::iterator del_it = it;
				++it;
				pipe_files.erase(del_it);
			}
			else
			{
				++it;
			}
		}

		for(std::map<std::string, SExitInformation>::iterator it=exit_information.begin();
			it!=exit_information.end();)
		{
			if(currtime - it->second.created > pipe_file_timeout)
			{
				std::map<std::string, SExitInformation>::iterator del_it = it;
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
	const std::string& server_token, const std::string& identity, int64 folder_items, int64 metadata_id)
{
	if(public_fn.empty() || next(public_fn, 0, "urbackup/"))
	{
		return;
	}

	std::string sharename = getuntil("/", public_fn);
	if (sharename.empty())
	{
		sharename = public_fn;
	}

	CWData data;
	data.addString(public_fn);
	data.addString(local_fn);
	data.addInt64(folder_items);
	data.addInt64(metadata_id);
	data.addString(server_token);

	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter_cb = 
		metadata_callbacks.find(std::make_pair(sharename, identity));

	if(iter_cb!=metadata_callbacks.end())
	{
		data.addVoidPtr(iter_cb->second);
	}

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find("urbackup/FILE_METADATA|"+server_token);

	if(it!=pipe_files.end() && it->second.input_pipe!=NULL)
	{
		++active_shares[sharename + "|" + server_token];

		it->second.input_pipe->Write(data.getDataPtr(), data.getDataSize());
	}
	else
	{
		int dbg = 43;
	}
}

void PipeSessions::fileMetadataDone(const std::string & public_fn, const std::string& server_token)
{
	std::string sharename = getuntil("/", public_fn);
	if (sharename.empty())
	{
		sharename = public_fn;
	}

	IScopedLock lock(mutex);

	std::map<std::string, size_t>::iterator it = active_shares.find(sharename + "|" + server_token);

	if (it != active_shares.end())
	{
		--it->second;
		if (it->second == 0)
		{
			active_shares.erase(it);
		}
	}
}

bool PipeSessions::isShareActive(const std::string & sharename, const std::string& server_token)
{
	IScopedLock lock(mutex);

	std::map<std::string, size_t>::iterator it = active_shares.find(sharename + "|" + server_token);

	return it != active_shares.end();
}

void PipeSessions::metadataStreamEnd( const std::string& server_token )
{
	IScopedLock lock(mutex);

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find("urbackup/FILE_METADATA|"+server_token);

	if(it!=pipe_files.end() && it->second.input_pipe!=NULL)
	{
		CWData data;
		data.addString(std::string());
		data.addString(std::string());
		data.addInt64(0);
		data.addInt64(0);
		data.addString(std::string());

		it->second.input_pipe->Write(data.getDataPtr(), data.getDataSize());
	}
}

void PipeSessions::registerMetadataCallback( const std::string &name, const std::string& identity, IFileServ::IMetadataCallback* callback )
{
	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter
		= metadata_callbacks.find(std::make_pair(name, identity));

	if(iter!=metadata_callbacks.end())
	{
		delete iter->second;
	}

	metadata_callbacks[std::make_pair(name, identity)] = callback;
}

void PipeSessions::removeMetadataCallback( const std::string &name, const std::string& identity )
{
	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter
		= metadata_callbacks.find(std::make_pair(name, identity));

	if(iter!=metadata_callbacks.end())
	{
		delete iter->second;
		metadata_callbacks.erase(iter);
	}
}

std::string PipeSessions::getKey( const std::string& cmd, int& backupnum )
{
	if(cmd.empty())
	{
		return std::string();
	}

	std::vector<std::string> cmd_toks;
	TokenizeMail(cmd, cmd_toks, "|");
	std::string script_cmd = cmd_toks[0];

	if(script_cmd=="urbackup/FILE_METADATA" && cmd_toks.size()>1)
	{
		std::string server_token = cmd_toks[1];
		backupnum = 0;
		if(cmd_toks.size()>2)
		{
			backupnum = atoi(cmd_toks[2].c_str());
		}

		return script_cmd+"|"+server_token;
	}
	else
	{
		if(cmd_toks.size()>2)
		{
			backupnum = atoi(cmd_toks[2].c_str());
		}

		return cmd;
	}
}

