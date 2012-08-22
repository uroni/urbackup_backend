/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "FileServ.h"
#include "map_buffer.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include "CClientThread.h"
#include "../Interface/ThreadPool.h"

IMutex *FileServ::mutex=NULL;
std::vector<std::string> FileServ::identities;
bool FileServ::pause=false;

std::string getSystemServerName(void);

FileServ::FileServ(bool *pDostop, const std::wstring &pServername, THREADPOOL_TICKET serverticket)
	: servername(pServername), serverticket(serverticket)
{
	dostop=pDostop;
	if(servername.empty())
	{
		servername=Server->ConvertToUnicode(getSystemServerName());
	}
}

FileServ::~FileServ(void)
{
	delete dostop;
}

void FileServ::shareDir(const std::wstring &name, const std::wstring &path)
{
	add_share_path(name, path);
}

void FileServ::removeDir(const std::wstring &name)
{
	remove_share_path(name);
}

void FileServ::stopServer(void)
{
	*dostop=true;
	Server->getThreadPool()->waitFor(serverticket);
}

std::wstring FileServ::getShareDir(const std::wstring &name)
{
	return map_file(name);
}

void FileServ::addIdentity(const std::string &pIdentity)
{
	IScopedLock lock(mutex);
	identities.push_back(pIdentity);
}

void FileServ::init_mutex(void)
{
	mutex=Server->createMutex();
}

void FileServ::destroy_mutex(void)
{
	Server->destroy(mutex);
}

bool FileServ::checkIdentity(const std::string &pIdentity)
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<identities.size();++i)
	{
		if(identities[i]==pIdentity)
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

std::wstring FileServ::getServerName(void)
{
	return servername;
}

void FileServ::runClient(IPipe *cp)
{
	CClientThread cc(cp, NULL);
	cc();
}
