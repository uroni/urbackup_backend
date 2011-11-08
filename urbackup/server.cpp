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
#include "os_functions.h"
#include <memory.h>

const unsigned int waittime=50*1000; //1 min
const int max_offline=5;

BackupServer::BackupServer(IPipe *pExitpipe)
{
	exitpipe=pExitpipe;
}

void BackupServer::operator()(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER);
	ISettingsReader *settings=Server->createDBSettingsReader(Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER), "settings_db.settings");

#ifdef _WIN32
	std::wstring tmpdir;
	if(settings->getValue(L"tmpdir", &tmpdir) && !tmpdir.empty())
	{
		os_remove_nonempty_dir(tmpdir+os_file_sep()+L"urbackup_tmp");
		os_create_dir(tmpdir+os_file_sep()+L"urbackup_tmp");
		Server->setTemporaryDirectory(tmpdir+os_file_sep()+L"urbackup_tmp");
	}
	else
	{
		wchar_t tmpp[MAX_PATH];
		DWORD l;
		if((l=GetTempPathW(MAX_PATH, tmpp))==0 || l>MAX_PATH )
		{
			wcscpy_s(tmpp,L"C:\\");
		}

		std::wstring w_tmp=tmpp;

		if(!w_tmp.empty() && w_tmp[w_tmp.size()-1]=='\\')
		{
			w_tmp.erase(w_tmp.size()-1, 1);		}


		os_remove_nonempty_dir(w_tmp+os_file_sep()+L"urbackup_tmp");
		os_create_dir(w_tmp+os_file_sep()+L"urbackup_tmp");
		Server->setTemporaryDirectory(w_tmp+os_file_sep()+L"urbackup_tmp");
	}
#endif

	q_get_extra_hostnames=db->Prepare("SELECT id,hostname FROM extra_clients");
	q_update_extra_ip=db->Prepare("UPDATE extra_clients SET lastip=? WHERE id=?");

	FileClient fc;

	while(true)
	{
		findClients(fc);
		startClients(fc);

		if(!ServerStatus::isActive() && settings->getValue("autoshutdown", "false")=="true")
		{
			writestring("true", "urbackup/shutdown_now");
#ifdef _WIN32
			ExitWindowsEx(EWX_POWEROFF|EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION|SHTDN_REASON_MINOR_OTHER );
#endif
		}

		std::string r;
		exitpipe->Read(&r, waittime);
		if(r=="exit")
		{
			removeAllClients();
			exitpipe->Write("ok");
			Server->destroy(settings);
			db->destroyAllQueries();
			delete this;
			return;
		}
	}
}

void BackupServer::findClients(FileClient &fc)
{
	db_results res=q_get_extra_hostnames->Read();
	q_get_extra_hostnames->Reset();

	std::vector<in_addr> addr_hints;

	for(size_t i=0;i<res.size();++i)
	{
		unsigned int dest;
		bool b=os_lookuphostname(Server->ConvertToUTF8(res[i][L"hostname"]), &dest);
		if(b)
		{
			q_update_extra_ip->Bind((_i64)dest);
			q_update_extra_ip->Bind(res[i][L"id"]);
			q_update_extra_ip->Write();
			q_update_extra_ip->Reset();

			in_addr tmp;
			tmp.s_addr=dest;
			addr_hints.push_back(tmp);
		}
	}

	_u32 rc=fc.GetServers(true, addr_hints);
	while(rc==ERR_CONTINUE)
	{
		Server->wait(50);
		rc=fc.GetServers(false, addr_hints);
	}

	if(rc==ERR_ERROR)
	{
		Server->Log("Error in BackupServer::findClients rc==ERR_ERROR",LL_ERROR);
	}
}

void BackupServer::startClients(FileClient &fc)
{
	std::vector<std::wstring> names=fc.getServerNames();
	std::vector<sockaddr_in> servers=fc.getServers();

	for(size_t i=0;i<names.size();++i)
	{
		names[i]=Server->ConvertToUnicode(conv_filename(Server->ConvertToUTF8(names[i])));
		std::map<std::wstring, SClient>::iterator it=clients.find(names[i]);
		if( it==clients.end() )
		{
			Server->Log(L"New Backupclient: "+names[i]);
			ServerStatus::setOnline(names[i], true);
			IPipe *np=Server->createMemoryPipe();

			BackupServerGet *client=new BackupServerGet(np, servers[i], names[i]);
			Server->getThreadPool()->execute(client);

			SClient c;
			c.pipe=np;
			c.offlinecount=0;
			c.addr=servers[i];

			ServerStatus::setIP(names[i], c.addr.sin_addr.s_addr);

			clients.insert(std::pair<std::wstring, SClient>(names[i], c) );
		}
		else if(it->second.offlinecount<max_offline)
		{
			if(it->second.addr.sin_addr.s_addr==servers[i].sin_addr.s_addr)
			{
				it->second.offlinecount=0;
			}
			else
			{
				bool last=true;
				for(size_t j=i+1;j<names.size();++j)
				{
					if(names[j]==names[i])
					{
						last=false;
						break;
					}
				}
				if(last)
				{
					it->second.addr=servers[i];
					std::string msg;
					msg.resize(7+sizeof(sockaddr_in));
					msg[0]='a'; msg[1]='d'; msg[2]='d'; msg[3]='r'; msg[4]='e'; msg[5]='s'; msg[6]='s';
					memcpy(&msg[7], &it->second.addr, sizeof(sockaddr_in));
					it->second.pipe->Write(msg);

					char *ip=(char*)&it->second.addr.sin_addr.s_addr;

					Server->Log("New client address: "+nconvert((unsigned char)ip[0])+"."+nconvert((unsigned char)ip[1])+"."+nconvert((unsigned char)ip[2])+"."+nconvert((unsigned char)ip[3]), LL_INFO);

					ServerStatus::setIP(names[i], it->second.addr.sin_addr.s_addr);
				}
			}
		}
	}

	bool c=true;
	size_t maxi=0;
	while(c && !clients.empty())
	{
		c=false;

		size_t i_c=0;
		for(std::map<std::wstring, SClient>::iterator it=clients.begin();it!=clients.end();++it)
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
					Server->Log(L"Client exitet: "+it->first);
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
							Server->Log(L"Client finished: "+it->first);
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
	for(std::map<std::wstring, SClient>::iterator it=clients.begin();it!=clients.end();++it)
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