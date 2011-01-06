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

IMutex *FileServ::mutex=NULL;
std::vector<std::string> FileServ::identities;
bool FileServ::pause=false;

FileServ::FileServ(bool *pDostop)
{
	dostop=pDostop;
}

void FileServ::shareDir(const std::string &name, const std::wstring &path)
{
	add_share_path(name, path);
}

void FileServ::removeDir(const std::string &name)
{
	remove_share_path(name);
}

void FileServ::stopServer(void)
{
	*dostop=true;	
}

std::wstring FileServ::getShareDir(const std::string &name)
{
	return map_file(widen(name));
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