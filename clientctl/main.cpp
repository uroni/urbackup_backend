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

#include <iostream>
#include <string>
#include <stdlib.h>
#include "Connector.h"
#include "../stringtools.h"


int main(int argc, char *argv[])
{
	bool help=false;

        if(argc>1 && (std::string)argv[1]=="help")
        {
            help=true;
        }

	for(int i=0;i<argc;++i)
	{
                if((std::string)argv[i]=="--help")
			help=true;
	}
	if(argc==1 || help)
	{
		std::cout << "UrBackup CMD client v0.1" << std::endl;
		std::cout << "Start with urbackup_client_cmd start --incr --file or client_cmd --full --image" << std::endl;
		std::cout << "Command line options" << std::endl;
		std::cout << "[--help] Display this help" << std::endl;
		std::cout << "[--incr] Do incremental backup" << std::endl;
		std::cout << "[--full] Do full backup" << std::endl;
		std::cout << "[--image] Do image backup" << std::endl;
		std::cout << "[--file] Do file backup" << std::endl;
		std::cout << "[--pw file] Use password in file [default=pw.txt]" << std::endl;
		std::cout << "[--client hostname/ip] Start backup on this client [default=127.0.0.1]" << std::endl;
	}
	else
	{
		if(argc<2)
		{
			std::cerr << "Please specify an action like start/list/status" << std::endl;
			return 1;
		}

		std::string action = strlower(argv[1]);
		int incr=-1;
		int image=-1;
		std::string pw="pw.txt";
		std::string client="127.0.0.1";
		std::string path;
		bool has_backupid=false;
		int backupid=0;

		for(int i=2;i<argc;++i)
		{
			std::string ca=argv[i];
			std::string na;
			if(i+1<argc)
				na=argv[i+1];

			if(ca=="--incr" )
				incr=1;
			else if(ca=="--full")
				incr=0;
			else if(ca=="--image")
				image=1;
			else if(ca=="--file")
				image=0;
			else if(ca=="--pw")
			{
				if(!na.empty())
				{
					pw=na;
					++i;
				}
			}
			else if(ca=="--client")
			{
				if(!na.empty() && !next(na, 0, "-"))
				{
					client=na;
					++i;
				}
				else
				{
					std::cerr << "Please specify a hostname/ip for parameter --client" << std::endl;
					return 1;
				}
			}
			else if(ca=="--path")
			{
				if(!na.empty() && !next(na, 0, "-"))
				{
					path=na;
					++i;
				}
				else
				{
					std::cerr << "Please specify a path for parameter --path" << std::endl;
					return 1;
				}
			}
			else if(ca=="--backupid")
			{
				if(!na.empty() && !next(na, 0, "-"))
				{
					has_backupid=true;
					backupid = atoi(na.c_str());
					++i;
				}
				else
				{
					std::cerr << "Please specify a backup id for parameter --backupid" << std::endl;
					return 1;
				}
			}
		}

		Connector::setPWFile(pw);
		Connector::setClient(client);

		if(action=="start")
		{
			if( incr==-1 || image==-1 )
			{
				std::cerr << "Not enough arguments. Use --help to show available arguments." << std::endl;
				return 1;
			}

			int rc=0;
			if(image==0)
			{
				if(incr==0)
				{
					rc=Connector::startBackup(true);
				}
				else
				{
					rc=Connector::startBackup(false);
				}
			}
			else
			{
				if(incr==0)
				{
					rc=Connector::startImage(true);
				}
				else
				{
					rc=Connector::startImage(true);
				}
			}

			if(rc==2)
			{
				std::cout << "Backup is already running" << std::endl;
				return 2;
			}
			else if(rc==1)
			{
				std::cout << "Backup started" << std::endl;
				return 0;
			}
			else
			{
				std::cerr << "Error starting backup. No server found?" << std::endl;
				return 1;		
			}
		}
		else if(action=="status")
		{
			std::string status = Connector::getStatusDetail();
			if(!status.empty())
			{
				std::cout << status << std::endl;
				return 0;
			}
			else
			{
				std::cerr << "Error getting status" << std::endl;
				return 1;
			}
		}
		else if(action=="list")
		{
			int* pbackupid=NULL;
			if(has_backupid)
			{
				pbackupid = &backupid;
			}

            if(path.empty() && !has_backupid)
			{
				std::string filebackups = Connector::getFileBackupsList();

				if(!filebackups.empty())
				{
					std::cout << filebackups << std::endl;
					return 0;
				}
				else
				{
					std::cerr << "Error getting file backups" << std::endl;
					return 1;
				}
			}
			else
			{
				std::string filelist = Connector::getFileList(path, pbackupid);

				if(!filelist.empty())
				{
					std::cout << filelist << std::endl;
					return 0;
				}
				else
				{
					std::cerr << "Error getting file list" << std::endl;
					return 1;
				}
			}
		}
	}
	return 0;
}
