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
#include "PipeFileTar.h"
#include <string>
#include <stdlib.h>

volatile bool PipeSessions::do_stop = false;
IMutex* PipeSessions::mutex = NULL;
std::map<std::string, SPipeSession> PipeSessions::pipe_files;
std::map<std::string, SExitInformation> PipeSessions::exit_information;
std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*> PipeSessions::metadata_callbacks;
std::map<std::string, size_t> PipeSessions::active_shares;

const int64 pipe_file_timeout = 1*60*60*1000;
const int64 pipe_file_read_timeout = 30 * 60 * 1000;


IFile* PipeSessions::getFile(const std::string& cmd, ScopedPipeFileUser& pipe_file_user,
	const std::string& server_token, const std::string& ident, bool* sent_metadata, bool* tar_file,
	bool resume)
{
	if(cmd.empty())
	{
		return NULL;
	}

	IScopedLock lock(mutex);

	int backupnum;
	int64 fn_random = 0;
	std::string session_key = getKey(cmd, backupnum, fn_random);
	std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);

	if(it!=pipe_files.end() && it->second.backupnum!=backupnum)
	{
		removeFile(cmd);
		it=pipe_files.end();
	}

	if(it != pipe_files.end())
	{
		if (!it->second.metadata.empty())
		{
			transmitFileMetadata(cmd, it->second.metadata, server_token, ident);
			it->second.metadata.clear();
		}

		if (it->second.file == NULL)
		{
			if (sent_metadata != NULL)
			{
				*sent_metadata = true;
			}
			return NULL;
		}

		if (tar_file != NULL)
		{
			*tar_file = dynamic_cast<PipeFileTar*>(it->second.file)!=NULL;
		}

		pipe_file_user.reset(it->second.file);

		return it->second.file;
	}
	else if(!resume)
	{
		std::string script_cmd = getuntil("|", cmd);

		if(script_cmd=="urbackup/FILE_METADATA")
		{
			IPipe* cmd_pipe = Server->createMemoryPipe();
			FileMetadataPipe* nf = new FileMetadataPipe(cmd_pipe, cmd);
			
			pipe_files.insert(std::make_pair(session_key, SPipeSession(nf, cmd_pipe, backupnum, std::string()) ));

			pipe_file_user.reset(nf);
			return nf;
		}
		else
		{
			std::string output_filename = ExtractFileName(script_cmd);

			script_cmd.erase(script_cmd.size()-output_filename.size(), output_filename.size());

			bool l_tar_file = false;
			IPipeFile* pipe_file = NULL;
			std::string script_filename = FileServ::mapScriptOutputNameToScript(output_filename, l_tar_file, pipe_file);

			script_cmd = "\"" + script_cmd + script_filename +
				"\" \"" + output_filename + "\" "+convert(backupnum);

			if (tar_file != NULL)
			{
				*tar_file = l_tar_file;
			}

			IPipeFile* nf;
			if (pipe_file != NULL)
			{
				nf = pipe_file;
			}
			else if (!l_tar_file)
			{
				nf = new PipeFile(script_cmd);
			}
			else
			{
				nf = new PipeFileTar(script_cmd, backupnum, fn_random, output_filename, server_token, ident);
			}

			if(nf->getHasError())
			{
				delete nf;
				return NULL;
			}

			pipe_files.insert(std::make_pair(session_key, SPipeSession(nf, NULL, backupnum, std::string())));

			pipe_file_user.reset(nf);
			return nf;
		}		
	}

	return NULL;
}

void PipeSessions::injectPipeSession(const std::string & session_key, int backupnum, IPipeFile * pipe_file, const std::string& metadata)
{
	IScopedLock lock(mutex);
	std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);

	if (it != pipe_files.end() && it->second.backupnum != backupnum)
	{
		removeFile(session_key+ (backupnum>0 ? ("|"+convert(backupnum)) : ""));
		it = pipe_files.end();
	}

	if (it != pipe_files.end())
	{
		return;
	}

	pipe_files.insert(std::make_pair(session_key, SPipeSession(pipe_file, NULL, backupnum, metadata)));
}

void PipeSessions::init()
{
	mutex = Server->createMutex();

	Server->getThreadPool()->execute(new PipeSessions, "PipeSession: timeout");
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
	int64 fn_random = 0;
	std::string session_key = getKey(cmd, backupnum, fn_random);

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);
	if(it != pipe_files.end())
	{
		Server->Log("Removing pipe file "+cmd, LL_DEBUG);

		while (it->second.retrieving_exit_info)
		{
			if (it->second.retrieval_cond == NULL)
			{
				it->second.retrieval_cond = Server->createCondition();
			}
			it->second.retrieval_cond->wait(&lock);
		}

		if(!it->second.retrieved_exit_info
			&& it->second.file!=NULL)
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

		if(it->second.file!=NULL)
		{ 
			it->second.file->forceExitWait();

			while (it->second.file->hasUser())
			{	
				Server->wait(100);
			}

			delete it->second.file;
		}

		delete it->second.input_pipe;
		delete it->second.retrieval_cond;
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
	int64 fn_random = 0;
	std::string session_key = getKey(cmd, backupnum, fn_random);

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find(session_key);

	if (it != pipe_files.end())
	{
		while (it->second.retrieving_exit_info)
		{
			if (it->second.retrieval_cond== NULL)
			{
				it->second.retrieval_cond = Server->createCondition();
			}
			it->second.retrieval_cond->wait(&lock);
		}
	}

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
		if(it != pipe_files.end()
			&& !it->second.retrieved_exit_info)
		{
			it->second.retrieving_exit_info = true;

			int exit_code = -1;
			IPipeFile* f = it->second.file;

			lock.relock(NULL);

			std::string stderr_output = f->getStdErr();

			lock.relock(mutex);

			it->second.retrieving_exit_info = false;
			if (it->second.retrieval_cond!= NULL)
			{
				it->second.retrieval_cond->notify_all();
			}

			it->second.file->getExitCode(exit_code);
			SExitInformation exit_info(
				exit_code,
				stderr_output,
				Server->getTimeMS() );

			it->second.retrieved_exit_info=true;

			exit_information[session_key] = exit_info;

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
			if( !it->second.retrieving_exit_info
				 && (it->second.file!=NULL && currtime - it->second.file->getLastRead() > pipe_file_read_timeout)
					 || (it->second.file==NULL && currtime - it->second.addtime > pipe_file_timeout) )
			{
				if (it->second.file != NULL)
				{
					it->second.file->forceExitWait();

					while (it->second.file->hasUser())
					{
						Server->wait(1000);
					}

					delete it->second.file;
				}
				delete it->second.input_pipe;
				delete it->second.retrieval_cond;
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

IFileServ::IMetadataCallback* PipeSessions::transmitFileMetadata( const std::string& local_fn, const std::string& public_fn,
	const std::string& server_token, const std::string& identity, int64 folder_items, int64 metadata_id)
{
	if(public_fn.empty() || next(public_fn, 0, "urbackup/"))
	{
		return NULL;
	}

	std::string sharename = getuntil("/", public_fn);
	if (sharename.empty())
	{
		sharename = public_fn;
	}

	CWData data;
	data.addChar(METADATA_PIPE_SEND_FILE);
	data.addString(public_fn);
	data.addString(local_fn);
	data.addInt64(folder_items);
	data.addInt64(metadata_id);
	data.addString(server_token);

	IScopedLock lock(mutex);

	std::map<std::pair<std::string, std::string>, IFileServ::IMetadataCallback*>::iterator iter_cb = 
		metadata_callbacks.find(std::make_pair(sharename, identity));

	IFileServ::IMetadataCallback* ret = NULL;
	if(iter_cb!=metadata_callbacks.end())
	{
		data.addVoidPtr(iter_cb->second);
		ret = iter_cb->second;
	}

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find("urbackup/FILE_METADATA|"+server_token);

	if(it!=pipe_files.end() && it->second.input_pipe!=NULL)
	{
		++active_shares[sharename + "|" + server_token];

		it->second.input_pipe->Write(data.getDataPtr(), data.getDataSize());
	}
	else
	{
		Server->Log("No active metadata listener for \"urbackup/FILE_METADATA|" + server_token + "\"", LL_WARNING);
	}

	return ret;
}

void PipeSessions::transmitFileMetadata(const std::string & public_fn, const std::string& metadata, const std::string & server_token, const std::string & identity)
{
	if (public_fn.empty() || next(public_fn, 0, "urbackup/"))
	{
		return;
	}

	std::string sharename = getuntil("/", public_fn);
	if (sharename.empty())
	{
		sharename = public_fn;
	}

	CWData data;
	data.addChar(METADATA_PIPE_SEND_RAW);
	data.addString("f" + public_fn);
	data.addString(metadata);
	data.addString(server_token);

	IScopedLock lock(mutex);

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find("urbackup/FILE_METADATA|" + server_token);

	if (it != pipe_files.end() && it->second.input_pipe != NULL)
	{
		++active_shares[sharename + "|" + server_token];

		it->second.input_pipe->Write(data.getDataPtr(), data.getDataSize());
	}
	else
	{
		Server->Log("No active metadata listener for \"urbackup/FILE_METADATA|" + server_token + "\" (raw)", LL_WARNING);
	}
}

void PipeSessions::transmitFileMetadataAndFiledataWait(const std::string & public_fn, const std::string & metadata, const std::string & server_token, const std::string & identity, IFile* file)
{
	if (public_fn.empty() || next(public_fn, 0, "urbackup/"))
	{
		return;
	}

	std::string sharename = getuntil("/", public_fn);
	if (sharename.empty())
	{
		sharename = public_fn;
	}

	CWData metadatamsg;
	metadatamsg.addChar(METADATA_PIPE_SEND_RAW);
	metadatamsg.addString("f" + public_fn);
	metadatamsg.addString(metadata);
	metadatamsg.addString(server_token);

	CWData datamsg;
	datamsg.addChar(METADATA_PIPE_SEND_RAW_FILEDATA);
	datamsg.addString("f" + public_fn);
	datamsg.addVoidPtr(file);
	std::auto_ptr<IPipe> waitpipe(Server->createMemoryPipe());
	datamsg.addVoidPtr(waitpipe.get());
	datamsg.addString(server_token);

	IScopedLock lock(mutex);

	std::map<std::string, SPipeSession>::iterator it = pipe_files.find("urbackup/FILE_METADATA|" + server_token);

	if (it != pipe_files.end() && it->second.input_pipe != NULL)
	{
		++active_shares[sharename + "|" + server_token];

		it->second.input_pipe->Write(datamsg.getDataPtr(), datamsg.getDataSize());

		++active_shares[sharename + "|" + server_token];

		it->second.input_pipe->Write(metadatamsg.getDataPtr(), metadatamsg.getDataSize());

		lock.relock(NULL);
		std::string read_ret;
		waitpipe->Read(&read_ret);
	}
	else
	{
		Server->Log("No active metadata listener for \"urbackup/FILE_METADATA|" + server_token + "\" (send wait)", LL_WARNING);
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
		data.addChar(METADATA_PIPE_EXIT);

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

std::string PipeSessions::getKey( const std::string& cmd, int& backupnum, int64& fn_random)
{
	if(cmd.empty())
	{
		return std::string();
	}

	std::vector<std::string> cmd_toks;
	Tokenize(cmd, cmd_toks, "|");
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
		if(cmd_toks.size()>1)
		{
			backupnum = atoi(cmd_toks[1].c_str());
		}
		if (cmd_toks.size()>2)
		{
			fn_random = watoi64(cmd_toks[2].c_str());
		}

		return cmd;
	}
}

