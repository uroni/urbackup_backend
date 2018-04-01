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
#include "../Interface/File.h"
#include "../Interface/DatabaseCursor.h"
#include "../Interface/ThreadPool.h"
#include "ClientMain.h"
#include "database.h"
#include "../Interface/SettingsReader.h"
#include "server_status.h"
#include "server_cleanup.h"
#include "../stringtools.h"
#include "../urbackupcommon/os_functions.h"
#include "InternetServiceConnector.h"
#include "../Interface/PipeThrottler.h"
#include "snapshot_helper.h"
#include "dao/ServerBackupDao.h"
#include "FileBackup.h"
#include <memory.h>
#include <algorithm>
#include "ThrottleUpdater.h"
#include "../fsimageplugin/IFSImageFactory.h"

const int max_offline=5;

IPipeThrottler *BackupServer::global_internet_throttler=NULL;
IPipeThrottler *BackupServer::global_local_throttler=NULL;
IMutex *BackupServer::throttle_mutex=NULL;
bool BackupServer::file_snapshots_enabled=false;
bool BackupServer::image_snapshots_enabled = false;
bool BackupServer::reflink_is_copy = false;
BackupServer::ESnapshotMethod BackupServer::snapshot_method = BackupServer::ESnapshotMethod_None;
bool BackupServer::filesystem_transactions_enabled = false;
bool BackupServer::use_tree_hashing = false;
volatile bool BackupServer::update_delete_pending_clients=true;
IMutex* BackupServer::force_offline_mutex=NULL;
std::vector<std::string> BackupServer::force_offline_clients;
std::map<std::string, std::vector<std::string> >  BackupServer::virtual_clients;
IMutex* BackupServer::virtual_clients_mutex=NULL;
bool BackupServer::can_mount_images = false;
bool BackupServer::can_reflink = false;

extern IFSImageFactory *image_fak;

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
	ISettingsReader *settings=Server->createDBSettingsReader(Server->getDatabase(Server->getThreadID(),URBACKUPDB_SERVER), "settings_db.settings",
		"SELECT value FROM settings_db.settings WHERE key=? AND clientid=0");

	setupUseTreeHashing();

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
		writestring(backupfolder, "/etc/urbackup/backupfolder");
#endif
	}

	testSnapshotAvailability(db);
	testFilesystemTransactionAvailabiliy(db);
	if (image_fak != NULL)
	{
		can_mount_images = image_fak->initializeImageMounting();

		if (!can_mount_images)
		{
#ifdef _WIN32
			Server->Log("Image mounting disabled. Install ImDisk to enable image mounting.", LL_INFO);
#else
			std::string mount_helper = Server->getServerParameter("mount_helper");
			if (mount_helper.empty())
			{
				mount_helper = "urbackup_mount_helper";
			}

			std::string res;
			os_popen((mount_helper + " test 2>&1").c_str(), res);
			Server->Log("Image mounting disabled: " + trim(res), LL_INFO);
#endif
		}
	}
	runServerRecovery(db);

	q_get_extra_hostnames=db->Prepare("SELECT id,hostname FROM settings_db.extra_clients");
	q_update_extra_ip=db->Prepare("UPDATE settings_db.extra_clients SET lastip=? WHERE id=?");
	q_get_clientnames=db->Prepare("SELECT name FROM clients");
	q_update_lastseen = db->Prepare("UPDATE clients SET lastseen=datetime(?, 'unixepoch') WHERE id=?", false);

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
		std::string new_name = anames[i].first;
		if(std::find(client_info.begin(), client_info.end(), SClientInfo(new_name))!=client_info.end())
		{
			continue;
		}

		if (new_name.empty())
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
		client_info[i].name=conv_filename(client_info[i].name);

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

					new_name = conv_filename(new_name);

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
			ServerStatus::updateLastseen(curr_info.name);
			IPipe *np=Server->createMemoryPipe();

			update_existing_client_names=true;

			bool use_reflink=false;
#ifndef _WIN32
			if(file_snapshots_enabled)
				use_reflink=true;
#endif
			ClientMain *client=new ClientMain(np, curr_info.addr, curr_info.name, curr_info.subname, curr_info.mainname,
				curr_info.filebackup_group_offset, curr_info.internetclient, file_snapshots_enabled, image_snapshots_enabled, use_reflink);
			Server->getThreadPool()->execute(client, "client main");

			SClient c;
			c.pipe=np;
			c.offlinecount=0;
			c.changecount = 0;
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
			ServerStatus::updateLastseen(curr_info.name);

			bool found_lan=false;
			if(!curr_info.internetclient && it->second.internet_connection)
			{
				found_lan=true;
			}

			if(it->second.addr.sin_addr.s_addr==curr_info.addr.sin_addr.s_addr && !found_lan)
			{
				it->second.offlinecount = 0;
				it->second.changecount = 0;
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
				++it->second.changecount;
				if( (it->second.changecount>5 && none_fits)
					|| found_lan)
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

							SStatus status = ServerStatus::getStatus(it->first);
							if (status.lastseen != 0
								&& status.clientid!=0)
							{
								q_update_lastseen->Bind(status.lastseen);
								q_update_lastseen->Bind(status.clientid);
								q_update_lastseen->Write();
								q_update_lastseen->Reset();
							}

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

size_t BackupServer::throttleSpeedToBps(int speed_bps, bool & percent_max)
{
	size_t bps;
	if (speed_bps > 0)
	{
		bps = speed_bps;
		percent_max = false;
	}
	else if (speed_bps<-1)
	{
		bps = -1 * speed_bps - 1;
		percent_max = true;
	}
	else
	{
		bps = 0;
		percent_max = false;
	}
	return bps;
}

IPipeThrottler *BackupServer::getGlobalInternetThrottler(int speed_bps)
{
	IScopedLock lock(throttle_mutex);

	if(global_internet_throttler==NULL 
		&& (speed_bps == 0
			|| speed_bps == -1) )
	{
		return NULL;
	}

	if(global_internet_throttler==NULL)
	{
		global_internet_throttler=Server->createPipeThrottler(
			new ThrottleUpdater(-1, ThrottleScope_GlobalInternet));
	}
	else
	{
		bool percent_max;
		size_t bps = BackupServer::throttleSpeedToBps(speed_bps, percent_max);
		global_internet_throttler->changeThrottleLimit(bps,
			percent_max);
	}
	return global_internet_throttler;
}

IPipeThrottler *BackupServer::getGlobalLocalThrottler(int speed_bps)
{
	IScopedLock lock(throttle_mutex);

	if (global_local_throttler == NULL
		&& (speed_bps == 0
			|| speed_bps == -1) )
	{
		return NULL;
	}

	if(global_local_throttler==NULL)
	{
		global_local_throttler=Server->createPipeThrottler(
			new ThrottleUpdater(-1, ThrottleScope_GlobalLocal));
	}
	else
	{
		bool percent_max;
		size_t bps = BackupServer::throttleSpeedToBps(speed_bps, percent_max);
		global_local_throttler->changeThrottleLimit(bps,
			percent_max);
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

bool BackupServer::isFileSnapshotsEnabled()
{
	return file_snapshots_enabled;
}

bool BackupServer::isImageSnapshotsEnabled()
{
	return image_snapshots_enabled;
}

bool BackupServer::isReflinkCopy()
{
	return reflink_is_copy;
}

BackupServer::ESnapshotMethod BackupServer::getSnapshotMethod(bool image)
{
	if (image 
		&& snapshot_method == ESnapshotMethod_ZfsFile)
	{
		return ESnapshotMethod_Zfs;
	}
	return snapshot_method;
}

void BackupServer::testSnapshotAvailability(IDatabase *db)
{
	ISettingsReader *settings=Server->createDBSettingsReader(db, "settings_db.settings",
		"SELECT value FROM settings_db.settings WHERE key=? AND clientid=0");
	Server->Log("Testing if backup destination can handle subvolumes and snapshots...", LL_DEBUG);

	std::string snapshot_helper_cmd=Server->getServerParameter("snapshot_helper");
	if(!snapshot_helper_cmd.empty())
	{
		SnapshotHelper::setSnapshotHelperCommand(snapshot_helper_cmd);
	}

	std::string cow_mode=settings->getValue("cow_mode", "false");
	int method = SnapshotHelper::isAvailable();
	if(method<0)
	{
		if (cow_mode != "true")
		{
			image_snapshots_enabled = false;
			file_snapshots_enabled = false;
		}
		else
		{
			size_t n=10;
			do
			{
				Server->wait(10000);
				Server->Log("Waiting for backup destination to support snapshots...", LL_INFO);
				--n;
				method =SnapshotHelper::isAvailable();
			}
			while(method<0 && n>0);

			if(method >=0)
			{					
				enableSnapshots(method);
			}
			else
			{
				Server->Log("Copy on write mode is disabled, because the filesystem does not support it anymore.", LL_ERROR);
				db->BeginWriteTransaction();
				db->Write("DELETE FROM settings_db.settings WHERE key='cow_mode' AND clientid=0");
				db->Write("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('cow_mode', 'false', 0)");
				db->EndTransaction();

				image_snapshots_enabled = false;
				file_snapshots_enabled = false;
			}
		}
	}
	else
	{
		enableSnapshots(method);

		db->BeginWriteTransaction();
		db->Write("DELETE FROM settings_db.settings WHERE key='cow_mode' AND clientid=0");
		db->Write("INSERT INTO settings_db.settings (key, value, clientid) VALUES ('cow_mode', 'true', 0)");
		db->EndTransaction();
	}

	if(image_snapshots_enabled || file_snapshots_enabled)
	{
		if (file_snapshots_enabled && image_snapshots_enabled)
		{
			Server->Log("Backup destination does handle subvolumes and snapshots. Snapshots enabled for image and file backups.", LL_INFO);
		}
		else
		{
			Server->Log("Backup destination does handle subvolumes and snapshots. Snapshots enabled for image backups.", LL_INFO);
		}

		if (reflink_is_copy)
		{
			Server->Log("Emulating reflinks via copying", LL_INFO);
		}
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

void BackupServer::testFilesystemReflinkAvailability(IDatabase *db)
{
	Server->Log("Testing for reflinks in backup destination...", LL_DEBUG);

	ServerSettings settings(db);

	const std::string testfilename = "FGHTR654kgfdfg5764578kldsfsdfgre66juzfo";
	const std::string testfilename_reflinked = testfilename + "_2";

	std::string backupfolder = settings.getSettings()->backupfolder;

	writestring("test", backupfolder + os_file_sep() + testfilename);

	ScopedDeleteFn delete_fn(backupfolder + os_file_sep() + testfilename);

	bool b = os_create_hardlink(backupfolder + os_file_sep() + testfilename_reflinked,
		backupfolder + os_file_sep() + testfilename, true, NULL);

	if (!b)
	{
		return;
	}

	ScopedDeleteFn delete_fn_2(backupfolder + os_file_sep() + testfilename_reflinked);

	if (getFile(backupfolder + os_file_sep() + testfilename_reflinked)
		== "test")
	{
		can_reflink = true;
	}
}

bool BackupServer::isFilesystemTransactionEnabled()
{
	return filesystem_transactions_enabled;
}

bool BackupServer::canMountImages()
{
	return can_mount_images;
}

bool BackupServer::canReflink()
{
	return can_reflink;
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

void BackupServer::enableSnapshots(int method)
{
	if (method < 0)
	{
		return;
	}

	image_snapshots_enabled = true;

	snapshot_method = static_cast<ESnapshotMethod>(method);

	if (snapshot_method == ESnapshotMethod_Btrfs)
	{
		file_snapshots_enabled = true;
		can_reflink = true;
	}
	else if (snapshot_method == ESnapshotMethod_ZfsFile)
	{
		file_snapshots_enabled = true;
		reflink_is_copy = true;
	}
}

namespace
{
	class FileCleanups : public IThread
	{
	public:
		void operator()()
		{
			logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
			ScopedProcess recovery_process(std::string(), sa_startup_recovery, std::string(), logid, false, LOG_CATEGORY_CLEANUP);

			for (size_t i = 0; i < cleanups.size(); ++i)
			{
				std::string backupinfo = "[id=" + convert(cleanups[i]->getCleanupAction().backupid)+ ", clientid=" + convert(cleanups[i]->getCleanupAction().clientid) + "]";
				ServerLogger::Log(logid, "Deleting file backup " + backupinfo + "...", LL_WARNING);

				Server->getThreadPool()->executeWait(cleanups[i], "delete fbackup");
			}
			delete this;
		}

		std::vector<ServerCleanupThread*> cleanups;
	};
}

void BackupServer::runServerRecovery(IDatabase * db)
{
	logid_t logid = ServerLogger::getLogId(LOG_CATEGORY_CLEANUP);
	ScopedProcess recovery_process(std::string(), sa_startup_recovery, std::string(), logid, false, LOG_CATEGORY_CLEANUP);

	ServerSettings settings(db);

	std::string backupfolder = settings.getSettings()->backupfolder;

	if (!os_directory_exists(backupfolder))
	{
		ServerLogger::Log(logid, "Backupfolder \"" + backupfolder + "\" does not exist. Not running recovery.", LL_WARNING);
		return;
	}

	bool delete_missing = false;

	IQuery* q_all = db->Prepare("SELECT name, path FROM (backups INNER JOIN clients ON backups.clientid=clients.id) WHERE done=1");
	{
		ScopedDatabaseCursor cur(q_all->Cursor());
		db_single_result res;
		while (cur.next(res))
		{
			std::string backuppath = backupfolder + os_file_sep() + res["name"] + os_file_sep() + res["path"];
			if (os_directory_exists(os_file_prefix(backuppath)))
			{
				delete_missing = true;
				break;
			}
		}
	}

	if (!delete_missing)
	{
		q_all = db->Prepare("SELECT path FROM backup_images WHERE complete=1");
		ScopedDatabaseCursor cur(q_all->Cursor());

		db_single_result res;
		while (cur.next(res))
		{
			std::auto_ptr<IFile> f(Server->openFile(os_file_prefix(res["path"])));
			if (f.get()!=NULL)
			{
				delete_missing = true;
				break;
			}
		}
	}

	std::vector<db_single_result> to_delete;
	bool has_delete = false;

	size_t num_extra_check = 30;

	{
		IQuery* q_synced = db->Prepare("SELECT backups.id AS backupid, name, path, clientid, backuptime, done FROM (backups INNER JOIN clients ON backups.clientid=clients.id) WHERE synctime IS NOT NULL OR done=0 ORDER BY done ASC, synctime DESC");
		ScopedDatabaseCursor cur(q_synced->Cursor());
		db_single_result res;
		while (cur.next(res))
		{
			std::string backuppath = backupfolder + os_file_sep() + res["name"] + os_file_sep() + res["path"];
			std::string backupinfo = "[id=" + res["backupid"] + ", path=" + res["path"] + ", backuptime=" + res["backuptime"] + ", clientid=" + res["clientid"] + ", client=" + res["name"] + "]";

			if(!os_directory_exists(os_file_prefix(backuppath))
				&& os_directory_exists(os_file_prefix(backuppath + ".startup-del")) )
			{
				backuppath += ".startup-del";
			}

			bool delete_backup = false;

			if (res["done"] == "0")
			{
				ServerLogger::Log(logid, "File backup " + backupinfo + " is incomplete. Deleting it.", LL_WARNING);
				delete_backup = true;
			}
			else if (!os_directory_exists(os_file_prefix(backuppath)))
			{
				if (delete_missing)
				{
					ServerLogger::Log(logid, "File backup " + backupinfo + " does not exist on backup storage. Deleting it from database.", LL_WARNING);
					delete_backup = true;
				}
			}
			else
			{
				std::auto_ptr<IFile> sync_f(Server->openFile(os_file_prefix(backuppath + os_file_sep() + ".hashes" + os_file_sep() + sync_fn), MODE_READ));

				if (sync_f.get() == NULL)
				{
					ServerLogger::Log(logid, "File backup " + backupinfo + " is not synced to backup storage. Deleting it.", LL_WARNING);
					delete_backup = true;
				}
			}

			if (delete_backup)
			{
				to_delete.push_back(res);
			}
			else
			{
				--num_extra_check;
				if (num_extra_check == 0)
				{
					break;
				}
			}
		}
	}

	IQuery* q_set_done = db->Prepare("UPDATE backups SET done=0 WHERE id=?");
	std::auto_ptr<FileCleanups> file_cleanups(new FileCleanups);
	for (size_t i = 0; i < to_delete.size(); ++i)
	{
		has_delete = true;

		db_single_result res = to_delete[i];

		q_set_done->Bind(res["backupid"]);
		q_set_done->Write();
		q_set_done->Reset();

		std::string backupinfo = "[id=" + res["backupid"] + ", path=" + res["path"] + ", backuptime=" + res["backuptime"] + ", clientid=" + res["clientid"] + ", client=" + res["name"] + "]";
		ServerLogger::Log(logid, "Deleting file backup "+backupinfo+"...", LL_WARNING);
		std::string backuppath = backupfolder + os_file_sep() + res["name"] + os_file_sep() + res["path"];

		ServerCleanupThread* cleanup = new ServerCleanupThread(CleanupAction(backupfolder, watoi(res["clientid"]), watoi(res["backupid"]), true, NULL));
		if (!os_directory_exists(os_file_prefix(backuppath))
			&& os_directory_exists(os_file_prefix(backuppath + ".startup-del")))
		{
			file_cleanups->cleanups.push_back(cleanup);
		}
		else if (os_rename_file(os_file_prefix(backuppath), os_file_prefix(backuppath + ".startup-del")))
		{
			file_cleanups->cleanups.push_back(cleanup);
		}
		else
		{
			Server->getThreadPool()->executeWait(cleanup, "delete fbackup");
		}
	}

	if (!file_cleanups->cleanups.empty())
	{
		Server->getThreadPool()->execute(file_cleanups.release());
	}

	to_delete.clear();

	num_extra_check = 30;

	std::vector<std::pair<db_single_result, bool> > todelete_images;

	{
		IQuery* q_synced = db->Prepare("SELECT backup_images.id AS backupid, name, path, clientid, backuptime FROM (backup_images INNER JOIN clients ON backup_images.clientid=clients.id) WHERE synctime IS NOT NULL ORDER BY synctime DESC");
		ScopedDatabaseCursor cur(q_synced->Cursor());
		db_single_result res;
		while (cur.next(res))
		{
			std::string backupinfo = "[id=" + res["backupid"] + ", path=" + res["path"] + ", backuptime=" + res["backuptime"] + ", clientid=" + res["clientid"] + ", client=" + res["name"] + "]";

			std::auto_ptr<IFile> image_f(Server->openFile(os_file_prefix(res["path"]), MODE_READ));

			bool delete_backup = false;
			bool delete_db_entry = false;

			if (image_f.get() == NULL)
			{
				if (delete_missing)
				{
					ServerLogger::Log(logid, "Image backup " + backupinfo + " does not exist on backup storage. Deleting it from database.", LL_WARNING);
					delete_backup = true;
					delete_db_entry = true;
				}
			}
			else
			{
				std::auto_ptr<IFile> sync_f(Server->openFile(os_file_prefix(res["path"] + ".sync"), MODE_READ));

				if (sync_f.get() == NULL)
				{
					ServerLogger::Log(logid, "Image backup " + backupinfo + " is not synced to backup storage. Setting it to incomplete.", LL_WARNING);
					delete_backup = true;
				}
			}

			if (delete_backup)
			{
				todelete_images.push_back( std::make_pair(res, delete_db_entry) );
			}
			else
			{
				--num_extra_check;
				if (num_extra_check == 0)
				{
					break;
				}
			}
		}
	}

	IQuery* q_delete_image = db->Prepare("DELETE FROM backup_images WHERE id=?");
	IQuery* q_set_complete = db->Prepare("UPDATE backup_images SET complete=0 WHERE id=?");
	for (size_t i = 0; i < todelete_images.size(); ++i)
	{
		has_delete = true;

		db_single_result res = todelete_images[i].first;
		bool delete_db_entry = todelete_images[i].second;
		std::string backupinfo = "[id=" + res["backupid"] + ", path=" + res["path"] + ", backuptime=" + res["backuptime"] + ", clientid=" + res["clientid"] + ", client=" + res["name"] + "]";

		if (delete_db_entry)
		{
			ServerLogger::Log(logid, "Deleting image backup " + backupinfo + "...", LL_WARNING);

			ServerCleanupThread::deleteImage(logid, res["name"], res["path"]);

			q_delete_image->Bind(watoi(res["backupid"]));
			q_delete_image->Write();
			q_delete_image->Reset();
		}
		else
		{
			ServerLogger::Log(logid, "Setting image backup " + backupinfo + " to incomplete...", LL_WARNING);

			q_set_complete->Bind(watoi(res["backupid"]));
			q_set_complete->Write();
			q_set_complete->Reset();
		}
	}

	todelete_images.clear();

	std::string db_backupfolder = backupfolder + os_file_sep() + "urbackup";

	{
		IQuery* q_last_backups = db->Prepare("SELECT b.id AS backupid FROM backups b WHERE NOT EXISTS (SELECT id FROM backups a WHERE a.id!=b.id AND a.backuptime>b.backuptime AND a.done=1 AND a.clientid=b.clientid AND a.tgroup=b.tgroup) AND b.done=1");
		ScopedDatabaseCursor cur(q_last_backups->Cursor());
		db_single_result res;
		while (cur.next(res))
		{
			std::string clientlist_fn = "clientlist_b_" + res["backupid"] + ".ub";

			if (!Server->fileExists("urbackup" + os_file_sep() + clientlist_fn))
			{
				std::string bfile = findFile(db_backupfolder, clientlist_fn);

				if (!bfile.empty())
				{
					ServerLogger::Log(logid, "Recovering " + clientlist_fn + " from backup...");

					copy_file(bfile, "urbackup" + os_file_sep() + clientlist_fn);
				}
				else
				{
					ServerLogger::Log(logid, "File " + clientlist_fn + " is missing/cannot be accessed", LL_WARNING);
				}
			}
		}
	}

	if (has_delete)
	{
		ServerLogger::Log(logid, "Start-up recovery finished.", LL_INFO);
	}

	db->destroyAllQueries();
}

std::string BackupServer::findFile(const std::string & path, const std::string & fn)
{
	std::vector<SFile> files = getFiles(path);
	for (size_t i = 0; i < files.size(); ++i)
	{
		if (files[i].isdir)
		{
			std::string res = findFile(path + os_file_sep() + files[i].name, fn);
			if (!res.empty())
			{
				return res;
			}
		}
		else if(files[i].name==fn)
		{
			return path + os_file_sep() + fn;
		}
	}

	return std::string();
}

void BackupServer::setupUseTreeHashing()
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	db_results res = db->Read("SELECT tvalue FROM misc WHERE tkey='hashing'");

	if (res.empty())
	{
		db_results res_c = db->Read("SELECT COUNT(*) AS c FROM backups");
		
		if (!res_c.empty() && watoi(res_c[0]["c"]) == 0)
		{
			db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('hashing', 'treehash')");
			use_tree_hashing = true;
		}
		else
		{
			db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('hashing', 'sha512')");
			use_tree_hashing = false;
		}
	}
	else
	{
		std::string val = res[0]["tvalue"];

		if (val == "treehash")
		{
			use_tree_hashing = true;
		}
		else
		{
			use_tree_hashing = false;
		}
	}
}

void BackupServer::setVirtualClients( const std::string& clientname, const std::string& new_virtual_clients )
{
	std::vector<std::string> toks;
	Tokenize(new_virtual_clients, toks, "|");

	IScopedLock lock(virtual_clients_mutex);
	virtual_clients[clientname] = toks;
}

bool BackupServer::useTreeHashing()
{
	return use_tree_hashing;
}

