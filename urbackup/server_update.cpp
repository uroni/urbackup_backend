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

#ifndef CLIENT_ONLY

#include "server_update.h"
#include "../downloadplugin/IDownloadFactory.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "os_functions.h"
#include <stdlib.h>

extern IDownloadFactory *download_fak;

ServerUpdate::ServerUpdate(void)
{
}

bool ServerUpdate::waitForDownload(IFileDownload *dl)
{
	while(true)
	{
		uchar rc=dl->download();
		if(rc==FD_ERR_SUCCESS)
		{
			return true;
		}
		else if(rc==FD_ERR_TIMEOUT || rc==FD_ERR_FILE_DOESNT_EXIST || rc==FD_ERR_ERROR )
		{
			return false;
		}
	}
}

void ServerUpdate::operator()(void)
{
	IFileDownload *dl=download_fak->createFileDownload();

	if(!Server->getServerParameter("http_proxy").empty())
	{
		dl->setProxy(Server->getServerParameter("http_proxy"), (unsigned short)atoi(Server->getServerParameter("http_proxy_port").c_str()));
	}

	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return;
	std::string tfn=tmp->getFilename();
	Server->destroy(tmp);
	dl->download("http://www.urserver.de/urbackup/update/version.txt", tfn);

	if(!waitForDownload(dl))
	{
		download_fak->destroyFileDownload(dl);
		return;
	}

	std::string version=getFile(tfn);
	std::string curr_version=getFile("urbackup/version.txt");
	if(curr_version.empty()) curr_version="0";
	
	Server->deleteFile(tfn);

	if(atoi(version.c_str())>atoi(curr_version.c_str()))
	{
		dl->download("http://www.urserver.de/urbackup/update/UrBackupUpdate.sig", "urbackup/UrBackupUpdate.sig");
		if(!waitForDownload(dl))
		{
			download_fak->destroyFileDownload(dl);
			return;
		}
		dl->download("http://www.urserver.de/urbackup/update/UrBackupUpdate.exe", "urbackup/UrBackupUpdate.exe");
		if(!waitForDownload(dl))
		{
			download_fak->destroyFileDownload(dl);
			return;
		}

		writestring(version, "urbackup/version.txt");
	}

	download_fak->destroyFileDownload(dl);
}

#endif //CLIENT_ONLY