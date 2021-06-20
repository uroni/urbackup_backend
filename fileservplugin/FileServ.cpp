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

#include "FileServ.h"
#include "map_buffer.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "CClientThread.h"
#include "../Interface/ThreadPool.h"
#include <algorithm>
#include "CUDPThread.h"
#include "PipeSessions.h"
#include "PipeFileExt.h"
#include "FileMetadataPipe.h"

IMutex *FileServ::mutex=nullptr;
std::vector<FileServ::SIdentity> FileServ::identities;
bool FileServ::pause=false;
std::map<std::string, FileServ::SScriptMapping> FileServ::script_mappings;
IFileServ::ITokenCallbackFactory* FileServ::token_callback_factory = nullptr;
std::map<std::string, std::string> FileServ::fn_redirects;
std::map<std::pair<std::string, size_t>, size_t> FileServ::active_shares;
size_t FileServ::active_generation = 0;
FileServ::IReadErrorCallback* FileServ::read_error_callback = nullptr;
std::vector<std::string> FileServ::read_error_files;
std::map<std::pair<std::string, std::string>, IFileServ::CbtHashFileInfo> FileServ::cbt_hash_files;


FileServ::FileServ(bool *pDostop, const std::string &pServername, THREADPOOL_TICKET serverticket, bool use_fqdn)
	: servername(pServername), serverticket(serverticket)
{
	dostop=pDostop;
	if(servername.empty())
	{
		servername=getSystemServerName(use_fqdn);
	}
}

FileServ::~FileServ(void)
{
	delete dostop;
}

void FileServ::shareDir(const std::string &name, const std::string &path, const std::string& identity, bool allow_exec)
{
	add_share_path(name, path, identity, allow_exec);
}

bool FileServ::removeDir(const std::string &name, const std::string& identity)
{
	{
		IScopedLock lock(mutex);

		std::map<std::pair<std::string, std::string>, IFileServ::CbtHashFileInfo>::iterator it = cbt_hash_files.find(std::make_pair(name, identity));
		if (it != cbt_hash_files.end())
		{
			Server->destroy(it->second.cbt_hash_file);
			cbt_hash_files.erase(it);
		}
	}
	return remove_share_path(name, identity);
}

void FileServ::stopServer(void)
{
	*dostop=true;
	Server->getThreadPool()->waitFor(serverticket);
}

std::string FileServ::getShareDir(const std::string &name, const std::string& identity)
{
	bool allow_exec;
	return map_file(name, identity, allow_exec, nullptr);
}

void FileServ::addIdentity(const std::string &pIdentity, bool only_tunneled)
{
	SIdentity identity(pIdentity, only_tunneled);
	IScopedLock lock(mutex);
	if(std::find(identities.begin(), identities.end(), identity)
		== identities.end())
	{
		identities.push_back(identity);
	}
}

void FileServ::init_mutex(void)
{
	mutex=Server->createMutex();
}

void FileServ::destroy_mutex(void)
{
	Server->destroy(mutex);
}

bool FileServ::checkIdentity(const std::string &pIdentity, bool tunneled)
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i].identity==pIdentity
			&& (!identities[i].tunneled || tunneled) )
		{
			return true;
		}
	}
	return false;
}

void FileServ::setPause(bool b)
{
	pause=b;
}

bool FileServ::getPause(void)
{
	return pause;
}

bool FileServ::isPause(void)
{
	return pause;
}

std::string FileServ::getServerName(void)
{
	return servername;
}

void FileServ::runClient(IPipe *cp, std::vector<char>* extra_buffer)
{
	CClientThread cc(cp, nullptr, extra_buffer);
	cc();
}

bool FileServ::removeIdentity( const std::string &pIdentity )
{
	SIdentity identity(pIdentity, false);
	IScopedLock lock(mutex);
	std::vector<SIdentity>::iterator it = std::find(identities.begin(), identities.end(), identity);
	if(it!=identities.end())
	{
		identities.erase(it);
		return true;
	}
	else
	{
		return false;
	}
}

bool FileServ::getExitInformation(const std::string& cmd, std::string& stderr_data, int& exit_code)
{
	std::string pcmd;
	bool allow_exec;
	if (next(cmd, 0, "urbackup/TAR"))
	{
		std::string server_ident = getbetween("|", "|", cmd);
		pcmd = getafter("urbackup/TAR|" + server_ident + "|", cmd);

		std::string map_res = map_file(pcmd, std::string(), allow_exec, nullptr);
		if (map_res.empty())
		{
			return false;
		}
	}
	else
	{
		pcmd = map_file(cmd, std::string(), allow_exec, nullptr);
	}

	if (!allow_exec)
	{
		return false;
	}

	SExitInformation exit_info = PipeSessions::getExitInformation(pcmd);

	if(exit_info.created==0)
	{
		return false;
	}
	else
	{
		stderr_data = exit_info.outdata;
		exit_code = exit_info.rc;

		return true;
	}
}

void FileServ::addScriptOutputFilenameMapping(const std::string& script_output_fn, const std::string& script_fn, bool tar_file)
{
	IScopedLock lock(mutex);

	script_mappings[script_output_fn] = SScriptMapping(script_fn, tar_file, nullptr);
}

std::string FileServ::mapScriptOutputNameToScript(const std::string& script_fn, bool& tar_file, IPipeFile*& pipe_file)
{
	IScopedLock lock(mutex);

	std::map<std::string, SScriptMapping>::iterator it = script_mappings.find(script_fn);
	if(it!= script_mappings.end())
	{
		tar_file = it->second.tar_file;
		pipe_file = it->second.pipe_file;
		std::string script_fn = it->second.script_fn;
		if (pipe_file != nullptr)
		{
			script_mappings.erase(it);
		}
		return script_fn;
	}
	else
	{
		tar_file = false;
		pipe_file = nullptr;
		return script_fn;
	}
}

void FileServ::registerMetadataCallback( const std::string &name, const std::string& identity, IMetadataCallback* callback)
{
	PipeSessions::registerMetadataCallback(name, identity, callback);
}

void FileServ::removeMetadataCallback( const std::string &name, const std::string& identity )
{
	PipeSessions::removeMetadataCallback(name, identity);
}

void FileServ::registerTokenCallbackFactory( IFileServ::ITokenCallbackFactory* callback_factory )
{
	IScopedLock lock(mutex);

	token_callback_factory = callback_factory;
}

IFileServ::ITokenCallback* FileServ::newTokenCallback()
{
	IScopedLock lock(mutex);

	if(token_callback_factory==nullptr)
	{
		return nullptr;
	}

	return token_callback_factory->getTokenCallback();
}

size_t FileServ::incrShareActive(std::string sharename)
{
	if (sharename.find("/") != std::string::npos)
	{
		sharename = getuntil("/", sharename);
	}

	IScopedLock lock(mutex);
	++active_shares[std::make_pair(sharename, active_generation)];
	return active_generation;
}

void FileServ::decrShareActive(std::string sharename, size_t gen)
{
	if (sharename.find("/") != std::string::npos)
	{
		sharename = getuntil("/", sharename);
	}

	IScopedLock lock(mutex);

	std::map<std::pair<std::string, size_t>, size_t>::iterator it = active_shares.find(std::make_pair(sharename, gen));

	if (it != active_shares.end())
	{
		--it->second;
		if (it->second == 0)
		{
			active_shares.erase(it);
		}
	}
}

bool FileServ::hasActiveTransfers(const std::string& sharename, const std::string& server_token)
{
	if (PipeSessions::isShareActive(sharename, server_token))
	{
		return true;
	}

	IScopedLock lock(mutex);

	for (std::map<std::pair<std::string, size_t>, size_t>::iterator it = active_shares.begin();
		it != active_shares.end();++it)
	{
		if (it->first.first == server_token + "|" + sharename)
		{
			return true;
		}
	}

	return false;
}

bool FileServ::hasActiveTransfersGen(const std::string& sharename, const std::string& server_token, size_t gen)
{
	if (PipeSessions::isShareActiveGen(sharename, server_token, gen))
	{
		return true;
	}

	IScopedLock lock(mutex);

	for (std::map<std::pair<std::string, size_t>, size_t>::iterator it = active_shares.begin();
		it != active_shares.end(); ++it)
	{
		if (it->first.first == server_token + "|" + sharename &&
			it->first.second<=gen)
		{
			return true;
		}
	}

	return false;
}

bool FileServ::registerFnRedirect(const std::string & source_fn, const std::string & target_fn)
{
	IScopedLock lock(mutex);

	fn_redirects[source_fn] = target_fn;

	return false;
}

std::string FileServ::getRedirectedFn(const std::string & source_fn)
{
	IScopedLock lock(mutex);

	str_map::iterator it = fn_redirects.find(source_fn);

	if (it != fn_redirects.end())
	{
		return it->second;
	}
	else
	{
		return source_fn;
	}
}

void FileServ::callErrorCallback(std::string sharename, const std::string & filepath, int64 pos, const std::string& msg)
{
	{
		IScopedLock lock(mutex);
		read_error_files.push_back(filepath);
	}

	if (sharename.find("/") != std::string::npos)
	{
		sharename = getuntil("/", sharename);
	}

	if (read_error_callback != nullptr)
	{
		read_error_callback->onReadError(sharename, filepath, pos, msg);
	}
}

bool FileServ::hasReadError(const std::string & filepath)
{
	IScopedLock lock(mutex);
	return std::find(read_error_files.begin(),
		read_error_files.end(), filepath)!=read_error_files.end();
}

void FileServ::registerReadErrorCallback(IReadErrorCallback * cb)
{
	read_error_callback = cb;
}

void FileServ::clearReadErrors()
{
	IScopedLock lock(mutex);
	read_error_files.clear();
}

void FileServ::clearReadErrorFile(const std::string & filepath)
{
	IScopedLock lock(mutex);
	std::vector<std::string>::iterator it = std::find(read_error_files.begin(),
		read_error_files.end(), filepath);

	if (it != read_error_files.end())
	{
		read_error_files.erase(it);
	}
}

void FileServ::setCbtHashFile(const std::string & sharename, const std::string & identity, IFileServ::CbtHashFileInfo cbt_hash_file_info)
{
	IScopedLock lock(mutex);
	std::map<std::pair<std::string, std::string>, IFileServ::CbtHashFileInfo>::iterator it = cbt_hash_files.find(std::make_pair(sharename, identity));
	if (it != cbt_hash_files.end())
	{
		Server->destroy(it->second.cbt_hash_file);
	}
	cbt_hash_files[std::make_pair(sharename, identity)] = cbt_hash_file_info;
}

IFileServ::CbtHashFileInfo FileServ::getCbtHashFile(const std::string & sharename, const std::string & identity)
{
	IScopedLock lock(mutex);
	std::map<std::pair<std::string, std::string>, IFileServ::CbtHashFileInfo>::iterator it = cbt_hash_files.find(std::make_pair(sharename, identity));
	if (it != cbt_hash_files.end())
	{
		return it->second;
	}
	return IFileServ::CbtHashFileInfo();
}

void FileServ::registerScriptPipeFile(const std::string & script_fn, IPipeFileExt * pipe_file)
{
	IScopedLock lock(mutex);

	script_mappings[script_fn] = SScriptMapping(script_fn, false, new PipeFileExt(pipe_file, script_fn));
}

void FileServ::deregisterScriptPipeFile(const std::string & script_fn)
{
	IScopedLock lock(mutex);

	std::map<std::string, SScriptMapping>::iterator it = script_mappings.find(script_fn);
	if (it != script_mappings.end())
	{
		delete it->second.pipe_file;
		script_mappings.erase(it);
	}
}

IFileMetadataPipe* FileServ::getFileMetadataPipe()
{
	return new FileMetadataPipe;
}

size_t FileServ::incrActiveGeneration()
{
	IScopedLock lock(mutex);
	return active_generation++;
}
