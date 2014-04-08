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
#include "dao/ServerBackupDao.h"
#include <memory.h>
#include <algorithm>

const unsigned int waittime=50*1000; //1 min
const int max_offline=5;

IPipeThrottler *BackupServer::global_internet_throttler=NULL;
IPipeThrottler *BackupServer::global_local_throttler=NULL;
IMutex *BackupServer::throttle_mutex=NULL;
bool BackupServer::snapshots_enabled=false;
bool BackupServer::filesystem_transactions_enabled = false;
volatile bool BackupServer::update_delete_pending_clients=true;


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
	if( settings->getValue("use_tmpfiles", "")!="true" )
	{
		std::wstring backupfolder;
		if( settings->getValue(L"backupfolder", &backupfolder) )
		{
			std::wstring tmpfile_path=backupfolder+os_file_sep()+L"urbackup_tmp_files";

			Server->Log("Removing temporary files...");
			os_remove_nonempty_dir(tmpfile_path);
			Server->Log("Recreating temporary folder...");
			if(!os_create_dir(tmpfile_path))
			{
				Server->wait(5000);
				os_create_dir(tmpfile_path);
			}
		}
	}

	testSnapshotAvailability(db);
	testFilesystemTransactionAvailabiliy(db);

	q_get_extra_hostnames=db->Prepare("SELECT id,hostname FROM settings_db.extra_clients");
	q_update_extra_ip=db->Prepare("UPDATE settings_db.extra_clients SET lastip=? WHERE id=?");

	FileClient fc(true, "");

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
		exitpipe->Read(&r, 20000);
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
	std::vector<in_addr> addr_hints;
	if(q_get_extra_hostnames!=NULL)
	{
		db_results res=q_get_extra_hostnames->Read();
		q_get_extra_hostnames->Reset();

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
	}

	_u32 rc=fc.GetServers(true, addr_hints);
	while(rc==ERR_CONTINUE)
	{
		rc=fc.GetServers(false, addr_hints);

		if(exitpipe->isReadable())
		{
			break;
		}
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
	std::vector<std::string> endpoint_names;

	if(!internet_test_mode)
	{
		names=fc.getServerNames();
		servers=fc.getServers();
	}

	endpoint_names.resize(servers.size());

	for(size_t i=0;i<names.size();++i)
	{
		names[i]=Server->ConvertToUnicode(conv_filename(Server->ConvertToUTF8(names[i])));
	}

	std::vector<bool> inetclient;
	inetclient.resize(names.size());
	std::fill(inetclient.begin(), inetclient.end(), false);
	std::vector<std::pair<std::string, std::string> > anames=InternetServiceConnector::getOnlineClients();
	for(size_t i=0;i<anames.size();++i)
	{
		std::wstring new_name=Server->ConvertToUnicode(conv_filename(anames[i].first));
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
		endpoint_names.push_back(anames[i].second);
	}

	std::vector<bool> delete_pending_curr;
	delete_pending_curr.resize(names.size());
	maybeUpdateDeletePendingClients();

	for(size_t i=0;i<names.size();++i)
	{
		delete_pending_curr[i]=isDeletePendingClient(names[i]);
		if(delete_pending_curr[i])
			continue;

		std::map<std::wstring, SClient>::iterator it=clients.find(names[i]);
		if( it==clients.end() )
		{
			Server->Log(L"New Backupclient: "+names[i]);
			ServerStatus::setOnline(names[i], true);
			IPipe *np=Server->createMemoryPipe();

			bool use_reflink=false;
#ifndef _WIN32
			if(snapshots_enabled)
				use_reflink=true;
#endif
			BackupServerGet *client=new BackupServerGet(np, servers[i], names[i], inetclient[i], snapshots_enabled, use_reflink);
			Server->getThreadPool()->execute(client);

			SClient c;
			c.pipe=np;
			c.offlinecount=0;
			c.addr=servers[i];
			c.internet_connection=inetclient[i];

			if(c.internet_connection)
			{
				ServerStatus::setIP(names[i], inet_addr(endpoint_names[i].c_str()));
			}
			else
			{
				ServerStatus::setIP(names[i], c.addr.sin_addr.s_addr);
			}

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
				if(delete_pending_curr[i])
					continue;

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
					Server->Log(L"Client exited: "+it->first);
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

void BackupServer::testSnapshotAvailability(IDatabase *db)
{
	ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings");
	Server->Log("Testing if backup destination can handle subvolumes and snapshots...", LL_DEBUG);

	std::string snapshot_helper_cmd=Server->getServerParameter("snapshot_helper");
	if(!snapshot_helper_cmd.empty())
	{
		SnapshotHelper::setSnapshotHelperCommand(snapshot_helper_cmd);
	}

	std::string cow_mode=settings->getValue("cow_mode", "false");
	if(!SnapshotHelper::isAvailable())
	{
		if(cow_mode!="true")
		{
			snapshots_enabled=false;
		}
		else
		{
			size_t n=10;
			bool available;
			do
			{
				Server->wait(10000);
				Server->Log("Waiting for backup destination to support snapshots...", LL_INFO);
				--n;
				available=SnapshotHelper::isAvailable();
			}
			while(!available && n>0);

			if(available)
			{					
				snapshots_enabled=true;
			}
			else
			{
				Server->Log("Copy on write mode is disabled, because the filesystem does not support it anymore.", LL_ERROR);
				db->Write("INSERT OR REPLACE INTO settings_db.settings (key, value, clientid) VALUES ('cow_mode', 'false', 0)");
				snapshots_enabled=false;
			}
		}
	}
	else
	{
		snapshots_enabled=true;
		db->Write("INSERT OR REPLACE INTO settings_db.settings (key, value, clientid) VALUES ('cow_mode', 'true', 0)");
	}

	if(snapshots_enabled)
	{
		Server->Log("Backup destination does handle subvolumes and snapshots. Snapshots enabled.", LL_INFO);
	}
	else
	{
		Server->Log("Backup destination cannot handle subvolumes and snapshots. Snapshots disabled.", LL_INFO);
	}

	Server->destroy(settings);
}

void BackupServer::testFilesystemTransactionAvailabiliy( IDatabase *db )
{
	Server->Log("Testing if backup destination can handle filesystem transactions...", LL_DEBUG);

	ServerSettings settings(db);

	const std::wstring testdirname = L"FGHTR654kgfdfg5764578kldsfsdfgre66juzfo";
	const std::wstring testdirname_renamed = testdirname+L"_2";

	std::wstring backupfolder = settings.getSettings()->backupfolder;

	void* transaction = os_start_transaction();

	if(transaction==NULL)
	{
		filesystem_transactions_enabled=false;
		return;
	}

	os_create_dir(os_file_prefix(backupfolder+os_file_sep()+testdirname));

	if(!os_rename_file(os_file_prefix(backupfolder+os_file_sep()+testdirname), os_file_prefix(backupfolder+os_file_sep()+testdirname_renamed), transaction))
	{
		os_finish_transaction(transaction);
		filesystem_transactions_enabled=false;
		os_remove_dir(os_file_prefix(backupfolder+os_file_sep()+testdirname));
		return;
	}

	if(!os_finish_transaction(transaction))
	{
		filesystem_transactions_enabled=false;
		os_remove_dir(os_file_prefix(backupfolder+os_file_sep()+testdirname));
		return;
	}

	if(!os_directory_exists(os_file_prefix(backupfolder+os_file_sep()+testdirname_renamed)))
	{
		filesystem_transactions_enabled=false;
		os_remove_dir(os_file_prefix(backupfolder+os_file_sep()+testdirname));
	}
	else
	{
		filesystem_transactions_enabled=true;
		os_remove_dir(os_file_prefix(backupfolder+os_file_sep()+testdirname_renamed));
	}
}

bool BackupServer::isFilesystemTransactionEnabled()
{
	return filesystem_transactions_enabled;
}

void BackupServer::updateDeletePending()
{
	IScopedLock lock(throttle_mutex);
	update_delete_pending_clients=true;
}

bool BackupServer::isDeletePendingClient( const std::wstring& clientname )
{
	return std::find(delete_pending_clients.begin(), delete_pending_clients.end(), clientname)!=
		delete_pending_clients.end();
}

void BackupServer::maybeUpdateDeletePendingClients()
{
	IScopedLock lock(throttle_mutex);
	if(update_delete_pending_clients)
	{
		update_delete_pending_clients=false;
		IDatabase *db=Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER);
		ServerBackupDao backupDao(db);
		delete_pending_clients = backupDao.getDeletePendingClientNames();
	}
}

#endif //CLIENT_ONLY
