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
#include "../urbackupcommon/os_functions.h"
#include "InternetServiceConnector.h"
#include "../Interface/PipeThrottler.h"
#include "snapshot_helper.h"
#include <memory.h>

const unsigned int waittime=50*1000; //1 min
const int max_offline=5;

IPipeThrottler *BackupServer::global_internet_throttler=NULL;
IPipeThrottler *BackupServer::global_local_throttler=NULL;
IMutex *BackupServer::throttle_mutex=NULL;
bool BackupServer::snapshots_enabled=false;

BackupServer::BackupServer(IPipe *pExitpipe)
{
	throttle_mutex=Server->createMutex();
	exitpipe=pExitpipe;

	if(Server->getServerParameter("internet_test_mode")=="true")
		internet_test_mode=true;
	else
		internet_test_mode=false;
}

BackupServer::~BackupServer()
{
	Server->destroy(throttle_mutex);
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
		if(!os_create_dir(tmpdir+os_file_sep()+L"urbackup_tmp"))
		{
			Server->wait(5000);
			os_create_dir(tmpdir+os_file_sep()+L"urbackup_tmp");
		}
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
		if(!os_create_dir(w_tmp+os_file_sep()+L"urbackup_tmp"))
		{
			Server->wait(5000);
			os_create_dir(tmpdir+os_file_sep()+L"urbackup_tmp");
		}
		Server->setTemporaryDirectory(w_tmp+os_file_sep()+L"urbackup_tmp");
	}
#endif

	{
		Server->Log("Testing if backup destination can handle subvolumes and snapshots...", LL_DEBUG);
		if(!SnapshotHelper::isAvailable())
		{
			Server->Log("Backup destination cannot handle subvolumes and snapshots. Snapshots disabled.", LL_INFO);
			snapshots_enabled=false;
		}
		else
		{
			Server->Log("Backup destination does handle subvolumes and snapshots. Snapshots enabled.", LL_INFO);
			snapshots_enabled=true;
		}
	}

	q_get_extra_hostnames=db->Prepare("SELECT id,hostname FROM settings_db.extra_clients");
	q_update_extra_ip=db->Prepare("UPDATE settings_db.extra_clients SET lastip=? WHERE id=?");

	FileClient fc;

	Server->wait(1000);

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
	std::vector<std::wstring> names;
	std::vector<sockaddr_in> servers;

	if(!internet_test_mode)
	{
		names=fc.getServerNames();
		servers=fc.getServers();
	}

	for(size_t i=0;i<names.size();++i)
	{
		names[i]=Server->ConvertToUnicode(conv_filename(Server->ConvertToUTF8(names[i])));
	}

	std::vector<bool> inetclient;
	inetclient.resize(names.size());
	std::fill(inetclient.begin(), inetclient.end(), false);
	std::vector<std::string> anames=InternetServiceConnector::getOnlineClients();
	for(size_t i=0;i<anames.size();++i)
	{
		std::wstring new_name=Server->ConvertToUnicode(conv_filename(anames[i]));
		bool skip=false;
		for(size_t j=0;j<names.size();++j)
		{
			if( new_name==names[j] )
			{
				skip=true;
				break;
			}
		}
		if(skip)
			continue;

		names.push_back(new_name);
		inetclient.push_back(true);
		sockaddr_in n;
		memset(&n, 0, sizeof(sockaddr_in));
		servers.push_back(n);
	}

	for(size_t i=0;i<names.size();++i)
	{
		std::map<std::wstring, SClient>::iterator it=clients.find(names[i]);
		if( it==clients.end() )
		{
			Server->Log(L"New Backupclient: "+names[i]);
			ServerStatus::setOnline(names[i], true);
			IPipe *np=Server->createMemoryPipe();

			BackupServerGet *client=new BackupServerGet(np, servers[i], names[i], inetclient[i], snapshots_enabled);
			Server->getThreadPool()->execute(client);

			SClient c;
			c.pipe=np;
			c.offlinecount=0;
			c.addr=servers[i];
			c.internet_connection=inetclient[i];

			ServerStatus::setIP(names[i], c.addr.sin_addr.s_addr);

			clients.insert(std::pair<std::wstring, SClient>(names[i], c) );
		}
		else if(it->second.offlinecount<max_offline)
		{
			bool found_lan=false;
			if(inetclient[i]==false && it->second.internet_connection==true)
			{
				found_lan=true;
			}

			if(it->second.addr.sin_addr.s_addr==servers[i].sin_addr.s_addr && !found_lan)
			{
				it->second.offlinecount=0;
			}
			else
			{
				bool none_fits=true;
				for(size_t j=0;j<names.size();++j)
				{
					if(i!=j && names[j]==names[i] && it->second.addr.sin_addr.s_addr==servers[j].sin_addr.s_addr)
					{
						none_fits=false;
						break;
					}
				}
				if(none_fits || found_lan)
				{
					it->second.addr=servers[i];
					it->second.internet_connection=inetclient[i];
					std::string msg;
					msg.resize(7+sizeof(sockaddr_in)+1);
					msg[0]='a'; msg[1]='d'; msg[2]='d'; msg[3]='r'; msg[4]='e'; msg[5]='s'; msg[6]='s';
					memcpy(&msg[7], &it->second.addr, sizeof(sockaddr_in));
					msg[7+sizeof(sockaddr_in)]=(inetclient[i]==true?1:0);
					it->second.pipe->Write(msg);

					char *ip=(char*)&it->second.addr.sin_addr.s_addr;

					Server->Log("New client address: "+nconvert((unsigned char)ip[0])+"."+nconvert((unsigned char)ip[1])+"."+nconvert((unsigned char)ip[2])+"."+nconvert((unsigned char)ip[3]), LL_INFO);

					ServerStatus::setIP(names[i], it->second.addr.sin_addr.s_addr);

					it->second.offlinecount=0;
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
					SStatusAction s_action=ServerStatus::getStatus(it->first).statusaction;

					if(s_action==sa_none)
					{
						++it->second.offlinecount;
					}
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
			it->second.pipe->Write(msg.c_str());
			Server->wait(500);
		}
		Server->destroy(it->second.pipe);
	}
}

IPipeThrottler *BackupServer::getGlobalInternetThrottler(size_t speed_bps)
{
	IScopedLock lock(throttle_mutex);

	if(global_internet_throttler==NULL && speed_bps==0 )
		return NULL;

	if(global_internet_throttler==NULL)
	{
		global_internet_throttler=Server->createPipeThrottler(speed_bps);
	}
	else
	{
		global_internet_throttler->changeThrottleLimit(speed_bps);
	}
	return global_internet_throttler;
}

IPipeThrottler *BackupServer::getGlobalLocalThrottler(size_t speed_bps)
{
	IScopedLock lock(throttle_mutex);

	if(global_local_throttler==NULL && speed_bps==0 )
		return NULL;

	if(global_local_throttler==NULL)
	{
		global_local_throttler=Server->createPipeThrottler(speed_bps);
	}
	else
	{
		global_local_throttler->changeThrottleLimit(speed_bps);
	}
	return global_local_throttler;
}

void BackupServer::cleanupThrottlers(void)
{
	if(global_internet_throttler!=NULL)
	{
		Server->destroy(global_internet_throttler);
	}
	if(global_local_throttler!=NULL)
	{
		Server->destroy(global_local_throttler);
	}
}

bool BackupServer::isSnapshotsEnabled(void)
{
	return snapshots_enabled;
}

#endif //CLIENT_ONLY
