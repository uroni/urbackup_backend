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
#include "server.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/ThreadPool.h"
#include "ClientMain.h"
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
#include "ThrottleUpdater.h"

const int max_offline=5;

IPipeThrottler *BackupServer::global_internet_throttler=NULL;
IPipeThrottler *BackupServer::global_local_throttler=NULL;
IMutex *BackupServer::throttle_mutex=NULL;
bool BackupServer::snapshots_enabled=false;
bool BackupServer::filesystem_transactions_enabled = false;
volatile bool BackupServer::update_delete_pending_clients=true;
IMutex* BackupServer::force_offline_mutex=NULL;
std::vector<std::string> BackupServer::force_offline_clients;
std::map<std::string, std::vector<std::string> >  BackupServer::virtual_clients;
IMutex* BackupServer::virtual_clients_mutex=NULL;


BackupServer::BackupServer(IPipe *pExitpipe)
	: update_existing_client_names(true)
{
	throttle_mutex=Server->createMutex();
	exitpipe=pExitpipe;
	force_offline_mutex=Server->createMutex();
	virtual_clients_mutex=Server->createMutex();

	if(Server->getServerParameter("internet_only_mode")=="true")
		internet_only_mode=true;
	else
		internet_only_mode=false;
}

BackupServer::~BackupServer()
{
	Server->destroy(throttle_mutex);
	Server->destroy(force_offline_mutex);
	Server->destroy(virtual_clients_mutex);
}

void BackupServer::operator()(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER);
	ISettingsReader *settings=Server->createDBSettingsReader(Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER), "settings_db.settings");

#ifdef _WIN32
	std::string tmpdir;
	if(settings->getValue("tmpdir", &tmpdir) && !tmpdir.empty())
	{
		os_remove_nonempty_dir(tmpdir+os_file_sep()+"urbackup_tmp");
		if(!os_create_dir(tmpdir+os_file_sep()+"urbackup_tmp"))
		{
			Server->wait(5000);
			os_create_dir(tmpdir+os_file_sep()+"urbackup_tmp");
		}
		Server->setTemporaryDirectory(tmpdir+os_file_sep()+"urbackup_tmp");
	}
	else
	{
		wchar_t tmpp[MAX_PATH];
		DWORD l;
		if((l=GetTempPathW(MAX_PATH, tmpp))==0 || l>MAX_PATH )
		{
			wcscpy_s(tmpp,L"C:\\");
		}

		std::string w_tmp=Server->ConvertFromWchar(tmpp);

		if(!w_tmp.empty() && w_tmp[w_tmp.size()-1]=='\\')
		{
			w_tmp.erase(w_tmp.size()-1, 1);		}


		os_remove_nonempty_dir(w_tmp+os_file_sep()+"urbackup_tmp");
		if(!os_create_dir(w_tmp+os_file_sep()+"urbackup_tmp"))
		{
			Server->wait(5000);
			os_create_dir(w_tmp+os_file_sep()+"urbackup_tmp");
		}
		Server->setTemporaryDirectory(w_tmp+os_file_sep()+"urbackup_tmp");
	}
#endif
	std::string backupfolder;
	if( settings->getValue("backupfolder", &backupfolder) )
	{
		if( settings->getValue("use_tmpfiles", "")!="true" )
		{		
			std::string tmpfile_path=backupfolder+os_file_sep()+"urbackup_tmp_files";

			Server->Log("Removing temporary files...");
			os_remove_nonempty_dir(tmpfile_path);
			Server->Log("Recreating temporary folder...");
			if(!os_create_dir(tmpfile_path))
			{
				Server->wait(5000);
				os_create_dir(tmpfile_path);
			}
		}

#ifndef _WIN32
		os_create_dir("/etc/urbackup");
		writestring((backupfolder), "/etc/urbackup/backupfolder");
#endif
	}

	testSnapshotAvailability(db);
	testFilesystemTransactionAvailabiliy(db);

	q_get_extra_hostnames=db->Prepare("SELECT id,hostname FROM settings_db.extra_clients");
	q_update_extra_ip=db->Prepare("UPDATE settings_db.extra_clients SET lastip=? WHERE id=?");
	q_get_clientnames=db->Prepare("SELECT name FROM clients");

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
#ifndef _DEBUG
		int exitpipetimeout = 20000;
#else
		int exitpipetimeout = 0;
#endif
		exitpipe->Read(&r, exitpipetimeout);
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
			bool b=os_lookuphostname((res[i]["hostname"]), &dest);
			if(b)
			{
				q_update_extra_ip->Bind((_i64)dest);
				q_update_extra_ip->Bind(res[i]["id"]);
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

namespace
{
	struct SClientInfo
	{
		explicit SClientInfo(std::string name)
			: name(name), internetclient(false), delete_pending(false),
			filebackup_group_offset(0)
		{

		}

		SClientInfo() :
			internetclient(false), delete_pending(false),
				filebackup_group_offset(0)
		{
			
		}

		bool operator==(const SClientInfo& other) const
		{
			return name==other.name;
		}

		std::string name;
		std::string subname;
		std::string mainname;
		sockaddr_in addr;
		std::string endpoint_name;
		bool internetclient;
		bool delete_pending;
		int filebackup_group_offset;
	};
}

void BackupServer::startClients(FileClient &fc)
{
	std::vector<SClientInfo> client_info;

	if(!internet_only_mode)
	{
		std::vector<std::string> names=fc.getServerNames();
		std::vector<sockaddr_in> servers=fc.getServers();

		client_info.resize(servers.size());

		for(size_t i=0;i<names.size();++i)
		{
			client_info[i].name = names[i];
			client_info[i].addr = servers[i];
		}
	}

	maybeUpdateExistingClientsLower();

	std::vector<std::pair<std::string, std::string> > anames=InternetServiceConnector::getOnlineClients();
	for(size_t i=0;i<anames.size();++i)
	{
		std::string new_name = (anames[i].first);
		if(std::find(client_info.begin(), client_info.end(), SClientInfo(new_name))!=client_info.end())
		{
			continue;
		}

		SClientInfo new_client;
		new_client.name = new_name;
		memset(&(new_client.addr), 0, sizeof(sockaddr_in));
		new_client.endpoint_name = anames[i].second;
		new_client.internetclient = true;

		client_info.push_back(new_client);
	}

	for(size_t i=0;i<client_info.size();++i)
	{
		client_info[i].name=(conv_filename((client_info[i].name)));

		fixClientnameCase(client_info[i].name);

		client_info[i].mainname=client_info[i].name;
	}

	{
		IScopedLock lock(virtual_clients_mutex);

		for(size_t i=0,size=client_info.size();i<size;++i)
		{
			std::map<std::string, std::vector<std::string> >::iterator it=virtual_clients.find(client_info[i].name);

			if(it!=virtual_clients.end())
			{
				for(size_t j=0;j<it->second.size();++j)
				{
					std::string new_name = client_info[i].name+"["+it->second[j]+"]";

					new_name = (conv_filename((new_name)));

					fixClientnameCase(new_name);

					SClientInfo new_client = client_info[i];
					new_client.name = new_name;
					new_client.mainname = client_info[i].name;
					new_client.subname = it->second[j];
					new_client.filebackup_group_offset = static_cast<int>((j+1)*c_group_size);

					client_info.push_back(new_client);
				}
			}
		}
	}

	maybeUpdateDeletePendingClients();

	std::vector<std::string> local_force_offline_clients;
	{
		IScopedLock lock(force_offline_mutex);
		local_force_offline_clients = force_offline_clients;
	}

	for(size_t i=0;i<client_info.size();++i)
	{
		SClientInfo& curr_info = client_info[i];
		curr_info.delete_pending=isDeletePendingClient(curr_info.name);
		curr_info.delete_pending = curr_info.delete_pending ||
			std::find(local_force_offline_clients.begin(), local_force_offline_clients.end(), curr_info.name) != local_force_offline_clients.end();

		if(curr_info.delete_pending)
		{
			continue;
		}

		std::map<std::string, SClient>::iterator it=clients.find(curr_info.name);
		if( it==clients.end() )
		{
			Server->Log("New Backupclient: "+curr_info.name);
			ServerStatus::setOnline(curr_info.name, true);
			IPipe *np=Server->createMemoryPipe();

			update_existing_client_names=true;

			bool use_reflink=false;
#ifndef _WIN32
			if(snapshots_enabled)
				use_reflink=true;
#endif
			ClientMain *client=new ClientMain(np, curr_info.addr, curr_info.name, curr_info.subname, curr_info.mainname,
				curr_info.filebackup_group_offset, curr_info.internetclient, snapshots_enabled, use_reflink);
			Server->getThreadPool()->execute(client);

			SClient c;
			c.pipe=np;
			c.offlinecount=0;
			c.addr=curr_info.addr;
			c.internet_connection=curr_info.internetclient;

			if(c.internet_connection)
			{
				ServerStatus::setIP(curr_info.name, inet_addr(curr_info.endpoint_name.c_str()));
			}
			else
			{
				ServerStatus::setIP(curr_info.name, c.addr.sin_addr.s_addr);
			}

			clients.insert(std::pair<std::string, SClient>(curr_info.name, c) );
		}
		else if(it->second.offlinecount<max_offline)
		{
			bool found_lan=false;
			if(!curr_info.internetclient && it->second.internet_connection)
			{
				found_lan=true;
			}

			if(it->second.addr.sin_addr.s_addr==curr_info.addr.sin_addr.s_addr && !found_lan)
			{
				it->second.offlinecount=0;
			}
			else
			{
				bool none_fits=true;
				for(size_t j=0;j<client_info.size();++j)
				{
					if(i!=j && client_info[j].name==curr_info.name 
						&& it->second.addr.sin_addr.s_addr==client_info[j].addr.sin_addr.s_addr)
					{
						none_fits=false;
						break;
					}
				}
				if(none_fits || found_lan)
				{
					it->second.addr=curr_info.addr;
					it->second.internet_connection=curr_info.internetclient;
					std::string msg;
					msg.resize(7+sizeof(sockaddr_in)+1);
					msg[0]='a'; msg[1]='d'; msg[2]='d'; msg[3]='r'; msg[4]='e'; msg[5]='s'; msg[6]='s';
					memcpy(&msg[7], &it->second.addr, sizeof(sockaddr_in));
					msg[7+sizeof(sockaddr_in)]=(curr_info.internetclient?1:0);
					it->second.pipe->Write(msg);

					char *ip=(char*)&it->second.addr.sin_addr.s_addr;

					Server->Log("New client address: "+convert((unsigned char)ip[0])+"."+convert((unsigned char)ip[1])+"."+convert((unsigned char)ip[2])+"."+convert((unsigned char)ip[3]), LL_INFO);

					ServerStatus::setIP(curr_info.name, it->second.addr.sin_addr.s_addr);

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
		for(std::map<std::string, SClient>::iterator it=clients.begin();it!=clients.end();++it)
		{
			bool found=false;
			for(size_t i=0;i<client_info.size();++i)
			{
				if(client_info[i].delete_pending)
					continue;

				if( it->first==client_info[i].name )
				{
					found=true;
					break;
				}
			}
			if( found==false || it->second.offlinecount>max_offline)
			{
				if(it->second.offlinecount==max_offline)
				{
					Server->Log("Client exited: "+it->first);
					it->second.pipe->Write("exit");
					++it->second.offlinecount;
					ServerStatus::setCommPipe(it->first, NULL);
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
							ServerStatus::removeStatus(it->first);
							Server->destroy(it->second.pipe);

							{
								IScopedLock lock(virtual_clients_mutex);

								std::map<std::string, std::vector<std::string> >::iterator virt_it=virtual_clients.find(it->first);
								if(virt_it!=virtual_clients.end())
								{
									virtual_clients.erase(virt_it);
								}
							}

							IScopedLock lock(force_offline_mutex);
							std::vector<std::string>::iterator off_iter = std::find(force_offline_clients.begin(),
								force_offline_clients.end(),
								it->first);
							if(off_iter!= force_offline_clients.end())
							{
								Server->Log("Client was forced offline: "+it->first);
								force_offline_clients.erase(off_iter);
							}

							clients.erase(it);
							maxi=i_c;
							c=true;

							update_existing_client_names=true;

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
					if(ServerStatus::getStatus(it->first).processes.empty())
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
	for(std::map<std::string, SClient>::iterator it=clients.begin();it!=clients.end();++it)
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
		global_internet_throttler=Server->createPipeThrottler(speed_bps,
			new ThrottleUpdater(-1, ThrottleScope_GlobalInternet));
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
		global_local_throttler=Server->createPipeThrottler(speed_bps,
			new ThrottleUpdater(-1, ThrottleScope_GlobalLocal));
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
				db->BeginWriteTransaction();
				db->Write("DELETE FROM settings_db.settings WHERE key='cow_mode' AND clientid=0");
				db->Write("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('cow_mode', 'false', 0)");
				db->EndTransaction();
				snapshots_enabled=false;
			}
		}
	}
	else
	{
		snapshots_enabled=true;
		db->BeginWriteTransaction();
		db->Write("DELETE FROM settings_db.settings WHERE key='cow_mode' AND clientid=0");
		db->Write("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('cow_mode', 'true', 0)");
		db->EndTransaction();
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

	const std::string testdirname = "FGHTR654kgfdfg5764578kldsfsdfgre66juzfo";
	const std::string testdirname_renamed = testdirname+"_2";

	std::string backupfolder = settings.getSettings()->backupfolder;

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

bool BackupServer::isDeletePendingClient( const std::string& clientname )
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

void BackupServer::forceOfflineClient( const std::string& clientname )
{
	IScopedLock lock(force_offline_mutex);

	Server->Log("Forcing offline client \""+clientname+"\"", LL_DEBUG);

	force_offline_clients.push_back(clientname);
}

void BackupServer::maybeUpdateExistingClientsLower()
{
	if(update_existing_client_names)
	{
		db_results res = q_get_clientnames->Read();

		existing_client_names.resize(res.size());
		existing_client_names_lower.resize(res.size());
		for(size_t i=0;i<res.size();++i)
		{
			existing_client_names[i]=res[i]["name"];
			existing_client_names_lower[i]=strlower(res[i]["name"]);
		}

		update_existing_client_names=false;
	}
}

void BackupServer::fixClientnameCase( std::string& clientname )
{
	std::string name_lower = strlower(clientname);
	for(size_t j=0;j<existing_client_names_lower.size();++j)
	{
		if(existing_client_names_lower[j]==name_lower)
		{
			clientname=existing_client_names[j];
			break;
		}
	}
}

void BackupServer::setVirtualClients( const std::string& clientname, const std::string& new_virtual_clients )
{
	std::vector<std::string> toks;
	TokenizeMail(new_virtual_clients, toks, "|");

	IScopedLock lock(virtual_clients_mutex);
	virtual_clients[clientname] = toks;
}

