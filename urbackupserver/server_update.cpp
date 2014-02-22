/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#define _CRT_SECURE_NO_WARNINGS

#include "server_update.h"
#include "../downloadplugin/IDownloadFactory.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include <stdlib.h>

extern IDownloadFactory *download_fak;

namespace
{
	void set_proxy_from_env(IFileDownload *dl)
	{
		char *http_proxy_s=getenv("http_proxy");
		if(http_proxy_s)
		{
			std::string http_proxy=http_proxy_s;
			if(!http_proxy.empty())
			{
				unsigned short proxy_port=8080;
				std::string proxy_name;
				if(http_proxy.find(":")!=std::string::npos)
				{
					proxy_port=atoi(getafter(":", http_proxy).c_str());
					proxy_name=getbetween("//", ":", http_proxy);

					if(proxy_name.empty())
					{
						proxy_name=getuntil(":", http_proxy);
					}
				}
				else
				{
					proxy_name=getbetween("//", "/", http_proxy);

					if(proxy_name.empty())
					{
						proxy_name=getafter("//", http_proxy);

						if(proxy_name.empty())
						{
							proxy_name=http_proxy;
						}
					}
				}

				if(!proxy_name.empty())
				{
					dl->setProxy(proxy_name, proxy_port);
				}
			}
		}
	}
}

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
			Server->Log("Downloaded file successfully", LL_INFO);
			return true;
		}
		else if(rc==FD_ERR_TIMEOUT || rc==FD_ERR_SOCKET_ERROR || rc==FD_ERR_FILE_DOESNT_EXIST || rc==FD_ERR_ERROR )
		{
			Server->Log("Download err: "+dl->getErrorString(rc), LL_ERROR);
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
	else
	{
		set_proxy_from_env(dl);
	}


	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return;
	std::string tfn=tmp->getFilename();
	Server->destroy(tmp);
	Server->Log("Downloading version file...", LL_INFO);
	dl->download("http://update1.urbackup.org/version.txt", tfn);

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
		Server->Log("Downloading signature...", LL_INFO);
		dl->download("http://update1.urbackup.org/UrBackupUpdate.sig", "urbackup/UrBackupUpdate.sig");
		if(!waitForDownload(dl))
		{
			download_fak->destroyFileDownload(dl);
			return;
		}
		Server->Log("Downloading update...", LL_INFO);
		dl->download("http://update1.urbackup.org/UrBackupUpdate.exe", "urbackup/UrBackupUpdate.exe");
		if(!waitForDownload(dl))
		{
			download_fak->destroyFileDownload(dl);
			return;
		}

		writestring(version, "urbackup/version.txt");
	}

	download_fak->destroyFileDownload(dl);
}