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

#include "server.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/ThreadPool.h"
#include "server_get.h"
#include "database.h"
#include "../Interface/SettingsReader.h"
#include "server_status.h"
#include "../stringtools.h"
#include <memory.h>

const unsigned int waittime=50*1000; //1 min
const int max_offline=5;

BackupServer::BackupServer(IPipe *pExitpipe)
{
	exitpipe=pExitpipe;
}

void BackupServer::operator()(void)
{
	ISettingsReader *settings=Server->createDBSettingsReader(Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER), "settings");
	backupfolder=settings->getValue(L"backupfolder", L"");
	backupfolder_uncompr=settings->getValue(L"backupfolder_uncompr", backupfolder);


	FileClient fc;

	while(true)
	{
		findClients(fc);
		startClients(fc);

		if(!ServerStatus::isActive() && settings->getValue("autoshutdown", "false")=="true")
		{
			writestring("true", "urbackup/shutdown_now");
		}

		std::string r;
		exitpipe->Read(&r, waittime);
		if(r=="exit")
		{
			removeAllClients();
			exitpipe->Write("ok");
			Server->destroy(settings);
			delete this;
			return;
		}
	}
}

void BackupServer::findClients(FileClient &fc)
{
	_u32 rc=fc.GetServers(true);
	while(rc==ERR_CONTINUE)
	{
		Server->wait(50);
		rc=fc.GetServers(false);
	}

	if(rc==ERR_ERROR)
	{
		Server->Log("Error in BackupServer::findClients rc==ERR_ERROR",LL_ERROR);
	}
}

void BackupServer::startClients(FileClient &fc)
{
	std::vector<std::string> names=fc.getServerNames();
	std::vector<sockaddr_in> servers=fc.getServers();

	for(size_t i=0;i<names.size();++i)
	{
		std::map<std::string, SClient>::iterator it=clients.find(names[i]);
		if( it==clients.end() )
		{
			Server->Log("New Backupclient: "+names[i]);
			ServerStatus::setOnline(names[i], true);
			IPipe *np=Server->createMemoryPipe();

			BackupServerGet *client=new BackupServerGet(np, servers[i], names[i], backupfolder, backupfolder_uncompr);
			Server->getThreadPool()->execute(client);

			SClient c;
			c.pipe=np;
			c.offlinecount=0;
			c.addr=servers[i];

			clients.insert(std::pair<std::string, SClient>(names[i], c) );
		}
		else if(it->second.offlinecount<max_offline)
		{
			if(it->second.addr.sin_addr.s_addr==servers[i].sin_addr.s_addr)
			{
				it->second.offlinecount=0;
			}
			else
			{
				it->second.addr=servers[i];
				std::string msg;
				msg.resize(7+sizeof(sockaddr_in));
				msg[0]='a'; msg[1]='d'; msg[2]='d'; msg[3]='r'; msg[4]='e'; msg[5]='s'; msg[6]='s';
				memcpy(&msg[7], &it->second.addr, sizeof(sockaddr_in));
				it->second.pipe->Write(msg);
			}
		}
	}

	bool c=true;
	size_t maxi=0;
	while(c && !clients.empty())
	{
		c=false;

		size_t i_c=0;
		for(std::map<std::string, SClient>::iterator it=clients.begin();it!=clients.end();++it)
		{
			bool found=false;
			for(size_t i=0;i<names.size();++i)
			{
				if( it->first==names[i] )
				{
					found=true;
					break;
				}
			}
			if( found==false || it->second.offlinecount>max_offline)
			{
				if(it->second.offlinecount==max_offline)
				{
					Server->Log("Client exitet: "+it->first);
					it->second.pipe->Write("exit");
					++it->second.offlinecount;
					ServerStatus::setOnline(it->first, false);
				}
				else if(it->second.offlinecount>max_offline)
				{
					std::string msg;
					std::vector<std::string> msgs;
					while(it->second.pipe->Read(&msg,0)>0)
					{
						if(msg!="ok")
						{
							msgs.push_back(msg);
						}
						else
						{
							Server->Log("Client finished: "+it->first);
							ServerStatus::setDone(it->first, true);
							Server->destroy(it->second.pipe);
							clients.erase(it);
							maxi=i_c;
							c=true;
							break;
						}
					}
					if( c==false )
					{
						for(size_t i=0;i<msgs.size();++i)
						{
							it->second.pipe->Write(msgs[i]);
						}
					}
					else
					{
						break;
					}
				}
				else if(i_c>=maxi)
				{
					++it->second.offlinecount;
				}
			}
			++i_c;
		}
	}
}

void BackupServer::removeAllClients(void)
{
	for(std::map<std::string, SClient>::iterator it=clients.begin();it!=clients.end();++it)
	{
		it->second.pipe->Write("exitnow");
		std::string msg;
		while(msg!="ok")
		{
			it->second.pipe->Read(&msg);
			it->second.pipe->Write(msg);
			Server->wait(500);
		}
		Server->destroy(it->second.pipe);
	}
}


#endif //CLIENT_ONLY