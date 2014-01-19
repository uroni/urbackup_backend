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
#include "server_get.h"
#include "server_ping.h"
#include "database.h"
#include "../stringtools.h"
#include "fileclient/FileClient.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../common/data.h"
#include "../urbackupcommon/settingslist.h"
#include "server_channel.h"
#include "server_log.h"
#include "InternetServiceConnector.h"
#include "server_update_stats.h"
#include "../urbackupcommon/escape.h"
#include "server_running.h"
#include "server_cleanup.h"
#include "treediff/TreeDiff.h"
#include "../urlplugin/IUrlFactory.h"
#include "../urbackupcommon/mbrdata.h"
#include "../Interface/PipeThrottler.h"
#include "snapshot_helper.h"
#include "server.h"
#include <algorithm>
#include <memory.h>
#include <time.h>
#include <stdio.h>
#include <limits.h>
#include <memory>
#include <assert.h>
#ifndef NAME_MAX
#define NAME_MAX _POSIX_NAME_MAX
#endif

extern IUrlFactory *url_fak;
extern std::string server_identity;
extern std::string server_token;

const unsigned short serviceport=35623;
const unsigned int full_backup_construct_timeout=4*60*60*1000;
const unsigned int shadow_copy_timeout=30*60*1000;
const unsigned int check_time_intervall_tried_backup=30*60*1000;
const unsigned int check_time_intervall=5*60*1000;
const unsigned int status_update_intervall=1000;
const size_t minfreespace_min=50*1024*1024;
const unsigned int curr_image_version=1;
const unsigned int ident_err_retry_time=1*60*1000;
const unsigned int ident_err_retry_time_retok=10*60*1000;
const unsigned int c_filesrv_connect_timeout=10000;
const unsigned int c_internet_fileclient_timeout=30*60*1000;


int BackupServerGet::running_backups=0;
int BackupServerGet::running_file_backups=0;
IMutex *BackupServerGet::running_backup_mutex=NULL;
IMutex *BackupServerGet::tmpfile_mutex=NULL;
size_t BackupServerGet::tmpfile_num=0;

BackupServerGet::BackupServerGet(IPipe *pPipe, sockaddr_in pAddr, const std::wstring &pName,
	     bool internet_connection, bool use_snapshots, bool use_reflink)
	: internet_connection(internet_connection), server_settings(NULL), client_throttler(NULL),
	  use_snapshots(use_snapshots), use_reflink(use_reflink), local_hash(NULL), bsh(NULL),
	  bsh_ticket(ILLEGAL_THREADPOOL_TICKET), bsh_prepare(NULL), bsh_prepare_ticket(ILLEGAL_THREADPOOL_TICKET)
{
	q_update_lastseen=NULL;
	pipe=pPipe;
	clientaddr=pAddr;
	clientaddr_mutex=Server->createMutex();
	clientname=pName;
	clientid=0;

	hashpipe=NULL;
	hashpipe_prepare=NULL;

	do_full_backup_now=false;
	do_incr_backup_now=false;
	do_update_settings=false;
	do_full_image_now=false;
	do_incr_image_now=false;
	
	can_backup_images=true;

	filesrv_protocol_version=0;
	file_protocol_version=1;
	image_protocol_version=0;
	update_version=0;

	set_settings_version=0;
	tcpstack.setAddChecksum(internet_connection);

	settings=NULL;
	settings_client=NULL;
	SSettings tmp = {};
	curr_intervals = tmp;
	
}

BackupServerGet::~BackupServerGet(void)
{
	if(q_update_lastseen!=NULL)
		unloadSQL();

	Server->destroy(clientaddr_mutex);

	if(client_throttler!=NULL)
	{
		Server->destroy(client_throttler);
	}

	if(settings!=NULL) Server->destroy(settings);
	if(settings_client!=NULL) Server->destroy(settings_client);

	if(local_hash!=NULL)
	{
		local_hash->deinitDatabase();
		delete local_hash;
	}
}

void BackupServerGet::init_mutex(void)
{
	running_backup_mutex=Server->createMutex();
	tmpfile_mutex=Server->createMutex();
}

void BackupServerGet::destroy_mutex(void)
{
	Server->destroy(running_backup_mutex);
	Server->destroy(tmpfile_mutex);
}

void BackupServerGet::unloadSQL(void)
{
	db->destroyQuery(q_update_lastseen);
	db->destroyQuery(q_update_full);
	db->destroyQuery(q_update_incr);
	db->destroyQuery(q_create_backup);
	db->destroyQuery(q_get_last_incremental);
	db->destroyQuery(q_get_last_incremental_complete);
	db->destroyQuery(q_set_last_backup);
	db->destroyQuery(q_update_setting);
	db->destroyQuery(q_insert_setting);
	db->destroyQuery(q_set_complete);
	db->destroyQuery(q_update_image_full);
	db->destroyQuery(q_update_image_incr);
	db->destroyQuery(q_create_backup_image);
	db->destroyQuery(q_set_image_complete);
	db->destroyQuery(q_set_last_image_backup);
	db->destroyQuery(q_get_last_incremental_image);
	db->destroyQuery(q_set_image_size);
	db->destroyQuery(q_update_running_file);
	db->destroyQuery(q_update_running_image);
	db->destroyQuery(q_update_images_size);
	db->destroyQuery(q_set_done);
	db->destroyQuery(q_save_logdata);
	db->destroyQuery(q_get_unsent_logdata);
	db->destroyQuery(q_set_logdata_sent);
	db->destroyQuery(q_save_image_assoc);
	db->destroyQuery(q_get_users);
	db->destroyQuery(q_get_rights);
	db->destroyQuery(q_get_report_settings);
	db->destroyQuery(q_format_unixtime);
}

void BackupServerGet::operator ()(void)
{
	{
		bool c=true;
		while(c)
		{
			c=false;
			bool retok_err=false;
			bool b=sendClientMessage("ADD IDENTITY", "OK", L"Sending Identity to client \""+clientname+L"\" failed. Retrying soon...", 10000, false, LL_INFO, &retok_err);
			if(!b)
			{
				if(retok_err)
				{
					ServerStatus::setWrongIdent(clientname, true);
				}
				
				unsigned int retry_time=ident_err_retry_time;

				if(retok_err)
				{
					retry_time=ident_err_retry_time_retok;
				}

				c=true;
				std::string msg;
				pipe->Read(&msg, retry_time);
				if(msg=="exit" || msg=="exitnow")
				{
					pipe->Write("ok");
					Server->Log(L"server_get Thread for client \""+clientname+L"\" finished and the identity was not recognized", LL_INFO);

					delete this;
					return;
				}
			}
			else
			{
				ServerStatus::setWrongIdent(clientname, false);
			}
		}
	}

	if( clientname.find(L"##restore##")==0 )
	{
		ServerChannelThread channel_thread(this, -1, internet_connection);
		THREADPOOL_TICKET channel_thread_id=Server->getThreadPool()->execute(&channel_thread);

		while(true)
		{
			std::string msg;
			pipe->Read(&msg);
			if(msg=="exit" || msg=="exitnow" )
				break;
		}

		channel_thread.doExit();
		Server->getThreadPool()->waitFor(channel_thread_id);

		pipe->Write("ok");
		Server->Log(L"server_get Thread for client "+clientname+L" finished, restore thread");
		delete this;
		return;
	}

	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);
	DBScopedFreeMemory free_db_memory(db);

	server_settings=new ServerSettings(db);

	clientid=getClientID(db, clientname, server_settings, NULL);

	if(clientid==-1)
	{
		pipe->Write("ok");
		Server->Log(L"server_get Thread for client "+clientname+L" finished, because there were too many clients", LL_INFO);

		ServerStatus::setTooManyClients(clientname, true);
		ServerLogger::reset(clientid);
		delete server_settings;
		delete this;
		return;
	}

	settings=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings_db.settings WHERE key=? AND clientid=0");
	settings_client=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings_db.settings WHERE key=? AND clientid="+nconvert(clientid));

	delete server_settings;
	server_settings=new ServerSettings(db, clientid);

	if(!createDirectoryForClient())
	{
		pipe->Write("ok");
		delete server_settings;
		delete this;
		return;
	}

	prepareSQL();

	updateLastseen();	
	
	if(!updateCapabilities())
	{
		Server->Log(L"Could not get client capabilities", LL_ERROR);
		pipe->Write("ok");
		delete server_settings;
		delete this;
		return;
	}

	status.client=clientname;
	status.clientid=clientid;
	ServerStatus::setServerStatus(status);

	bool use_reflink=false;
#ifndef _WIN32
	if( use_snapshots )
		use_reflink=true;
#endif
	use_tmpfiles=server_settings->getSettings()->use_tmpfiles;
	use_tmpfiles_images=server_settings->getSettings()->use_tmpfiles_images;
	if(!use_tmpfiles)
	{
		tmpfile_path=server_settings->getSettings()->backupfolder+os_file_sep()+L"urbackup_tmp_files";
		os_create_dir(tmpfile_path);
		if(!os_directory_exists(tmpfile_path))
		{
			Server->Log("Could not create or see temporary folder in backuppath", LL_ERROR);
			use_tmpfiles=true;
		}
	}

	ServerChannelThread channel_thread(this, clientid, internet_connection);
	THREADPOOL_TICKET channel_thread_id=Server->getThreadPool()->execute(&channel_thread);

	if(internet_connection && server_settings->getSettings()->internet_calculate_filehashes_on_client)
	{
		local_hash=new BackupServerHash(NULL, clientid, use_snapshots, use_reflink, use_tmpfiles);
		local_hash->setupDatabase();
	}

	sendSettings();

	ServerLogger::Log(clientid, "Getting client settings...", LL_DEBUG);
	if(server_settings->getSettings()->allow_overwrite && !getClientSettings())
	{
		ServerLogger::Log(clientid, "Getting client settings failed. Retrying...", LL_INFO);
		Server->wait(200000);
		if(!getClientSettings())
		{
			ServerLogger::Log(clientid, "Getting client settings failed -1", LL_ERROR);
		}
	}

	ServerLogger::Log(clientid, "Sending backup incr intervall...", LL_DEBUG);
	sendClientBackupIncrIntervall();

	if(server_settings->getSettings()->autoupdate_clients)
	{
		checkClientVersion();
	}

	sendClientLogdata();

	update_sql_intervals(false);

	ServerStatus::setCommPipe(clientname, pipe);

	bool skip_checking=false;

	if( server_settings->getSettings()->startup_backup_delay>0 )
	{
		pipe->isReadable(server_settings->getSettings()->startup_backup_delay*1000);
		skip_checking=true;
	}

	ServerSettings server_settings_updated(db);

	bool do_exit_now=false;
	bool tried_backup=false;
	bool file_backup_err=false;
	
	while(true)
	{
		if(!skip_checking)
		{
			{
				bool settings_updated=false;
				server_settings_updated.getSettings(&settings_updated);
				if(do_update_settings || settings_updated)
				{
					ServerLogger::Log(clientid, "Getting client settings...", LL_DEBUG);
					do_update_settings=false;
					if(server_settings->getSettings()->allow_overwrite && !getClientSettings())
					{
						ServerLogger::Log(clientid, "Getting client settings failed -2", LL_ERROR);
					}
				}

				if(settings_updated)
				{
					sendSettings();
				}
			}

			update_sql_intervals(true);

			tried_backup=false;
			unsigned int ttime=Server->getTimeMS();
			status.starttime=ttime;
			has_error=false;
			bool hbu=false;
			bool r_success=false;
			bool r_image=false;
			bool disk_error=false;
			bool log_backup=true;
			r_incremental=false;
			pingthread=NULL;
			pingthread_ticket=ILLEGAL_THREADPOOL_TICKET;
			status.pcdone=0;
			status.hashqueuesize=0;
			status.prepare_hashqueuesize=0;
			backupid=-1;
			ServerStatus::setServerStatus(status);

			bool internet_no_full_file=(internet_connection && !server_settings->getSettings()->internet_full_file_backups );
			bool internet_no_images=(internet_connection && !server_settings->getSettings()->internet_image_backups );

			if(do_incr_image_now)
			{
				if(!can_backup_images)
					ServerLogger::Log(clientid, "Cannot do image backup because can_backup_images=false", LL_DEBUG);
				if(server_settings->getSettings()->no_images)
					ServerLogger::Log(clientid, "Cannot do image backup because no_images=true", LL_DEBUG);
				if(!isBackupsRunningOkay(false, false))
					ServerLogger::Log(clientid, "Cannot do image backup because isBackupsRunningOkay()=false", LL_DEBUG);
				if(!internet_no_images )
					ServerLogger::Log(clientid, "Cannot do image backup because internet_no_images=true", LL_DEBUG);
			}

			if(do_incr_backup_now)
			{
				if(file_backup_err)
					ServerLogger::Log(clientid, "Cannot do incremental file backup because file_backup_err=true", LL_DEBUG);
				if(server_settings->getSettings()->no_file_backups)
					ServerLogger::Log(clientid, "Cannot do incremental file backup because no_file_backups=true", LL_DEBUG);
				if(!isBackupsRunningOkay(false, true))
					ServerLogger::Log(clientid, "Cannot do incremental file backup because isBackupsRunningOkay()=false", LL_DEBUG);
			}

			bool image_hashed_transfer;
			if(internet_connection)
			{
				image_hashed_transfer= (server_settings->getSettings()->internet_image_transfer_mode=="hashed");
			}
			else
			{
				image_hashed_transfer= (server_settings->getSettings()->local_image_transfer_mode=="hashed");
			}

			ServerStatus::stopBackup(clientname, false);

			bool with_hashes=false;

			if( server_settings->getSettings()->internet_mode_enabled )
			{
				if( server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash")
					with_hashes=true;
			}

			if( server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash")
				with_hashes=true;

			if( !file_backup_err && !server_settings->getSettings()->no_file_backups && !internet_no_full_file &&
				( (isUpdateFull() && isInBackupWindow(server_settings->getBackupWindowFullFile())) || do_full_backup_now )
				&& isBackupsRunningOkay(true, true) )
			{
				hbu=true;
				ScopedActiveThread sat;

				status.statusaction=sa_full_file;
				ServerStatus::setServerStatus(status, true);

				createDirectoryForClient();

				ServerLogger::Log(clientid, "Starting full file backup...", LL_INFO);

				if(!constructBackupPath(with_hashes, use_snapshots, true))
				{
					ServerLogger::Log(clientid, "Cannot create Directory for backup (Server error)", LL_ERROR);
					r_success=false;
				}
				else
				{
					pingthread=new ServerPingThread(this);
					pingthread_ticket=Server->getThreadPool()->execute(pingthread);

					createHashThreads(use_reflink);

					r_success=doFullBackup(with_hashes, disk_error, log_backup);

					destroyHashThreads();

					if(do_full_backup_now)
					{
						log_backup=true;
					}
				}

				do_full_backup_now=false;
			}
			else if( !file_backup_err && !server_settings->getSettings()->no_file_backups
				&& ( (isUpdateIncr() && isInBackupWindow(server_settings->getBackupWindowIncrFile())) || do_incr_backup_now )
				&& isBackupsRunningOkay(true, true) )
			{
				hbu=true;
				ScopedActiveThread sat;

				status.statusaction=sa_incr_file;
				ServerStatus::setServerStatus(status, true);

				createDirectoryForClient();

				ServerLogger::Log(clientid, "Starting incremental file backup...", LL_INFO);
				
				r_incremental=true;
				if(!constructBackupPath(with_hashes, use_snapshots, false))
				{
					ServerLogger::Log(clientid, "Cannot create Directory for backup (Server error)", LL_ERROR);
					r_success=false;
				}
				else
				{
					pingthread=new ServerPingThread(this);
					pingthread_ticket=Server->getThreadPool()->execute(pingthread);

					createHashThreads(use_reflink);

					bool intra_file_diffs;
					if(internet_connection)
					{
						intra_file_diffs=(server_settings->getSettings()->internet_incr_file_transfer_mode=="blockhash");
					}
					else
					{
						intra_file_diffs=(server_settings->getSettings()->local_incr_file_transfer_mode=="blockhash");
					}

					r_success=doIncrBackup(with_hashes, intra_file_diffs, use_snapshots, disk_error, log_backup);

					destroyHashThreads();

					if(do_incr_backup_now)
					{
						log_backup=true;
					}
				}

				do_incr_backup_now=false;
			}
			else if(can_backup_images && !server_settings->getSettings()->no_images && !internet_no_images
				&& ( (isUpdateFullImage() && isInBackupWindow(server_settings->getBackupWindowFullImage())) || do_full_image_now)
				&& isBackupsRunningOkay(true, false) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_full_image;
				ServerStatus::setServerStatus(status, true);

				createDirectoryForClient();

				ServerLogger::Log(clientid, "Starting full image backup...", LL_INFO);
				
				r_image=true;

				pingthread=new ServerPingThread(this);
				pingthread_ticket=Server->getThreadPool()->execute(pingthread);

				r_success=true;
				std::vector<std::string> vols=server_settings->getBackupVolumes();
				for(size_t i=0;i<vols.size();++i)
				{
					if(isUpdateFullImage(vols[i]+":") || do_full_image_now)
					{
						int sysvol_id=-1;
						if(strlower(vols[i])=="c")
						{
							ServerLogger::Log(clientid, "Backing up SYSVOL...", LL_DEBUG);
				
							if(doImage("SYSVOL", L"", 0, 0, image_protocol_version>0))
							{
								sysvol_id=backupid;
							}
							ServerLogger::Log(clientid, "Backing up SYSVOL done.", LL_DEBUG);
						}
						bool b=doImage(vols[i]+":", L"", 0, 0, image_protocol_version>0);
						if(!b)
						{
							r_success=false;
							break;
						}
						else if(sysvol_id!=-1)
						{
							saveImageAssociation(backupid, sysvol_id);
						}
					}
				}

				do_full_image_now=false;
			}
			else if(can_backup_images && !server_settings->getSettings()->no_images && !internet_no_images
				&& ( (isUpdateIncrImage() && isInBackupWindow(server_settings->getBackupWindowIncrImage())) || do_incr_image_now)
				&& isBackupsRunningOkay(true, false) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_incr_image;
				ServerStatus::setServerStatus(status, true);

				createDirectoryForClient();

				ServerLogger::Log(clientid, "Starting incremental image backup...", LL_INFO);

				r_image=true;
				r_incremental=true;
			
				pingthread=new ServerPingThread(this);
				pingthread_ticket=Server->getThreadPool()->execute(pingthread);

				std::vector<std::string> vols=server_settings->getBackupVolumes();
				for(size_t i=0;i<vols.size();++i)
				{
					std::string letter=vols[i]+":";
					if(isUpdateIncrImage(letter) || do_incr_image_now)
					{
						int sysvol_id=-1;
						if(strlower(letter)=="c:")
						{
							ServerLogger::Log(clientid, "Backing up SYSVOL...", LL_DEBUG);
							if(doImage("SYSVOL", L"", 0, 0, image_protocol_version>0))
							{
								sysvol_id=backupid;
							}
							ServerLogger::Log(clientid, "Backing up SYSVOL done.", LL_DEBUG);
						}
						SBackup last=getLastIncrementalImage(letter);
						if(last.incremental==-2)
						{
							ServerLogger::Log(clientid, "Error retrieving last backup.", LL_ERROR);
							r_success=false;
							break;
						}
						else
						{
							r_success=doImage(letter, last.path, last.incremental+1, last.incremental_ref, image_hashed_transfer);
						}

						if(r_success && sysvol_id!=-1)
						{
							saveImageAssociation(backupid, sysvol_id);
						}

						if(!r_success)
							break;
					}
				}

				do_incr_image_now=false;
			}

			file_backup_err=false;

			if(local_hash!=NULL)
			{
				local_hash->copyFromTmpTable(true);
			}

			if(hbu)
			{
				if(disk_error)
				{
					has_error=true;
					r_success=false;

					ServerLogger::Log(clientid, "FATAL: Backup failed because of disk problems", LL_ERROR);
					sendMailToAdmins("Fatal error occured during backup", ServerLogger::getWarningLevelTextLogdata(clientid));
				}
			}

			if(hbu && !has_error)
			{
				if(!r_success)
				{
					sendBackupOkay(false);
				}
			}
			else if(hbu && has_error)
			{
				ServerLogger::Log(clientid, "Backup had errors. Deleting partial backup.", LL_ERROR);
				file_backup_err=true;
				if(backupid==-1)
				{
					if(use_snapshots)
					{
						if(!SnapshotHelper::removeFilesystem(clientname, backuppath_single) )
						{
							os_remove_nonempty_dir(backuppath);
						}
					}
					else
					{
						os_remove_nonempty_dir(backuppath);
					}	
				}
				else
				{				
					Server->getThreadPool()->executeWait(new ServerCleanupThread(CleanupAction(server_settings->getSettings()->backupfolder, clientid, backupid, true) ) );
				}
				tried_backup=true;
			}

			status.action_done=false;
			status.statusaction=sa_none;
			status.pcdone=100;
			

			ServerStatus::setServerStatus(status);

			unsigned int ptime=Server->getTimeMS()-ttime;
			if(hbu && !has_error)
			{
				ServerLogger::Log(clientid, L"Time taken for backing up client "+clientname+L": "+widen(PrettyPrintTime(ptime)), LL_INFO);
				if(!r_success)
				{
					ServerLogger::Log(clientid, "Backup failed", LL_ERROR);
				}
				else
				{
					updateLastBackup();
					setBackupComplete();
					ServerLogger::Log(clientid, "Backup succeeded", LL_INFO);
				}
				status.pcdone=100;
				ServerStatus::setServerStatus(status, true);
			}

			if( r_image )
			{
				ServerLogger::Log(clientid, L"Time taken for creating image of client "+clientname+L": "+widen(PrettyPrintTime(ptime)), LL_INFO);
				if(!r_success)
				{
					ServerLogger::Log(clientid, "Backup failed", LL_ERROR);
				}
				else
				{
					updateLastImageBackup();
					ServerLogger::Log(clientid, "Backup succeeded", LL_INFO);
				}
				status.pcdone=100;
				ServerStatus::setServerStatus(status, true);
			}

			if(hbu || r_image)
			{
				stopBackupRunning(!r_image);

				if(log_backup)
				{
					saveClientLogdata(r_image?1:0, r_incremental?1:0, r_success && !has_error);
					sendClientLogdata();
				}
				else
				{
					ServerLogger::reset(clientid);
				}
			}
			if(hbu)
			{
				ServerCleanupThread::updateStats(true);
			}

			if(pingthread!=NULL)
			{
				pingthread->setStop(true);
				Server->getThreadPool()->waitFor(pingthread_ticket);
				pingthread=NULL;
			}

			/* Predict sleep time -- 
			unsigned int wtime;
			if((unsigned int)update_freq_incr*1000>ptime)
			{
				wtime=update_freq_incr*1000-ptime;
			}
			else
			{
				wtime=0;
			}
			wtime+=60000;*/
		}

		std::string msg;
		if(tried_backup)
			pipe->Read(&msg, check_time_intervall_tried_backup);
		else
			pipe->Read(&msg, skip_checking?0:check_time_intervall);
		
		skip_checking=false;
		if(msg=="exit")
			break;
		else if(msg=="exitnow")
		{
			do_exit_now=true;
			break;
		}
		else if(msg=="START BACKUP INCR") do_incr_backup_now=true;
		else if(msg=="START BACKUP FULL") do_full_backup_now=true;
		else if(msg=="UPDATE SETTINGS") do_update_settings=true;
		else if(msg=="START IMAGE INCR") do_incr_image_now=true;
		else if(msg=="START IMAGE FULL") do_full_image_now=true;
		else if(msg.find("address")==0)
		{
			IScopedLock lock(clientaddr_mutex);
			memcpy(&clientaddr, &msg[7], sizeof(sockaddr_in) );
			internet_connection=(msg[7+sizeof(sockaddr_in)]==0)?false:true;
		}

		if(!msg.empty())
		{
			Server->Log("msg="+msg, LL_DEBUG);
		}
	}

	//destroy channel
	{
		Server->Log("Stopping channel...", LL_DEBUG);
		channel_thread.doExit();
		Server->getThreadPool()->waitFor(channel_thread_id);
	}
	
	
	Server->destroy(settings);
	settings=NULL;
	Server->destroy(settings_client);
	settings_client=NULL;
	delete server_settings;
	pipe->Write("ok");
	Server->Log(L"server_get Thread for client "+clientname+L" finished");

	delete this;
}

void BackupServerGet::prepareSQL(void)
{
	SSettings *s=server_settings->getSettings();
	q_update_lastseen=db->Prepare("UPDATE clients SET lastseen=CURRENT_TIMESTAMP WHERE id=?", false);
	q_update_full=db->Prepare("SELECT id FROM backups WHERE datetime('now','-"+nconvert(s->update_freq_full)+" seconds')<backuptime AND clientid=? AND incremental=0 AND done=1", false);
	q_update_incr=db->Prepare("SELECT id FROM backups WHERE datetime('now','-"+nconvert(s->update_freq_incr)+" seconds')<backuptime AND clientid=? AND complete=1 AND done=1", false);
	q_create_backup=db->Prepare("INSERT INTO backups (incremental, clientid, path, complete, running, size_bytes, done, archived, size_calculated) VALUES (?, ?, ?, 0, CURRENT_TIMESTAMP, -1, 0, 0, 0)", false);
	q_get_last_incremental=db->Prepare("SELECT incremental,path FROM backups WHERE clientid=? AND done=1 ORDER BY backuptime DESC LIMIT 1", false);
	q_get_last_incremental_complete=db->Prepare("SELECT incremental,path FROM backups WHERE clientid=? AND done=1 AND complete=1 ORDER BY backuptime DESC LIMIT 1", false);
	q_set_last_backup=db->Prepare("UPDATE clients SET lastbackup=(SELECT backuptime FROM backups WHERE id=?) WHERE id=?", false);
	q_update_setting=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=?", false);
	q_insert_setting=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,?)", false);
	q_set_complete=db->Prepare("UPDATE backups SET complete=1 WHERE id=?", false);
	q_update_image_full=db->Prepare("SELECT id FROM backup_images WHERE datetime('now','-"+nconvert(s->update_freq_image_full)+" seconds')<backuptime AND clientid=? AND incremental=0 AND complete=1 AND version="+nconvert(curr_image_version)+" AND letter=?", false);
	q_update_image_incr=db->Prepare("SELECT id FROM backup_images WHERE datetime('now','-"+nconvert(s->update_freq_image_incr)+" seconds')<backuptime AND clientid=? AND complete=1 AND version="+nconvert(curr_image_version)+" AND letter=?", false); 
	q_create_backup_image=db->Prepare("INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes, version, letter) VALUES (?, ?, ?, ?, 0, CURRENT_TIMESTAMP, 0, "+nconvert(curr_image_version)+",?)", false);
	q_set_image_size=db->Prepare("UPDATE backup_images SET size_bytes=? WHERE id=?", false);
	q_set_image_complete=db->Prepare("UPDATE backup_images SET complete=1 WHERE id=?", false);
	q_set_last_image_backup=db->Prepare("UPDATE clients SET lastbackup_image=(SELECT backuptime FROM backup_images WHERE id=?) WHERE id=?", false);
	q_get_last_incremental_image=db->Prepare("SELECT id,incremental,path FROM backup_images WHERE clientid=? AND incremental=0 AND complete=1 AND version="+nconvert(curr_image_version)+" AND letter=? ORDER BY backuptime DESC LIMIT 1", false);
	q_update_running_file=db->Prepare("UPDATE backups SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	q_update_running_image=db->Prepare("UPDATE backup_images SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	q_update_images_size=db->Prepare("UPDATE clients SET bytes_used_images=(SELECT bytes_used_images FROM clients WHERE id=?)+? WHERE id=?", false);
	q_set_done=db->Prepare("UPDATE backups SET done=1 WHERE id=?", false);
	q_save_logdata=db->Prepare("INSERT INTO logs (clientid, logdata, errors, warnings, infos, image, incremental) VALUES (?,?,?,?,?,?,?)", false);
	q_get_unsent_logdata=db->Prepare("SELECT id, strftime('%s', created) AS created, logdata FROM logs WHERE sent=0 AND clientid=?", false);
	q_set_logdata_sent=db->Prepare("UPDATE logs SET sent=1 WHERE id=?", false);
	q_save_image_assoc=db->Prepare("INSERT INTO assoc_images (img_id, assoc_id) VALUES (?,?)", false);
	q_get_users=db->Prepare("SELECT id FROM settings_db.si_users WHERE report_mail IS NOT NULL AND report_mail<>''", false);
	q_get_rights=db->Prepare("SELECT t_right FROM settings_db.si_permissions WHERE clientid=? AND t_domain=?", false);
	q_get_report_settings=db->Prepare("SELECT report_mail, report_loglevel, report_sendonly FROM settings_db.si_users WHERE id=?", false);
	q_format_unixtime=db->Prepare("SELECT datetime(?, 'unixepoch', 'localtime') AS time", false);
}

int BackupServerGet::getClientID(IDatabase *db, const std::wstring &clientname, ServerSettings *server_settings, bool *new_client)
{
	if(new_client!=NULL)
		*new_client=false;

	IQuery *q=db->Prepare("SELECT id FROM clients WHERE name=?",false);
	if(q==NULL) return -1;

	q->Bind(clientname);
	db_results res=q->Read();
	db->destroyQuery(q);

	if(res.size()>0)
		return watoi(res[0][L"id"]);
	else
	{
		IQuery *q_get_num_clients=db->Prepare("SELECT count(*) AS c FROM clients WHERE lastseen > date('now', '-2 month')", false);
		db_results res_r=q_get_num_clients->Read();
		q_get_num_clients->Reset();
		int c_clients=-1;
		if(!res_r.empty()) c_clients=watoi(res_r[0][L"c"]);

		db->destroyQuery(q_get_num_clients);

		if(server_settings==NULL || c_clients<server_settings->getSettings()->max_active_clients)
		{
			IQuery *q_insert_newclient=db->Prepare("INSERT INTO clients (name, lastseen,bytes_used_files,bytes_used_images) VALUES (?, CURRENT_TIMESTAMP, 0, 0)", false);
			q_insert_newclient->Bind(clientname);
			q_insert_newclient->Write();
			int rid=(int)db->getLastInsertID();
			q_insert_newclient->Reset();
			db->destroyQuery(q_insert_newclient);

			IQuery *q_insert_authkey=db->Prepare("INSERT INTO settings_db.settings (key,value, clientid) VALUES ('internet_authkey',?,?)", false);
			q_insert_authkey->Bind(ServerSettings::generateRandomAuthKey());
			q_insert_authkey->Bind(rid);
			q_insert_authkey->Write();
			q_insert_authkey->Reset();
			db->destroyQuery(q_insert_authkey);

			if(new_client!=NULL)
				*new_client=true;

			return rid;
		}
		else
		{
			Server->Log(L"Too many clients. Didn't accept client '"+clientname+L"'", LL_INFO);
			return -1;
		}
	}
}

SBackup BackupServerGet::getLastIncremental(void)
{
	q_get_last_incremental->Bind(clientid);
	db_results res=q_get_last_incremental->Read();
	q_get_last_incremental->Reset();
	if(res.size()>0)
	{
		SBackup b;
		b.incremental=watoi(res[0][L"incremental"]);
		b.path=res[0][L"path"];

		q_get_last_incremental_complete->Bind(clientid);
		res=q_get_last_incremental_complete->Read();
		q_get_last_incremental_complete->Reset();

		if(res.size()>0)
		{
			b.complete=res[0][L"path"];
		}

		b.incremental_ref=0;
		return b;
	}
	else
	{
		SBackup b;
		b.incremental=-2;
		b.incremental_ref=0;
		return b;
	}
}

SBackup BackupServerGet::getLastIncrementalImage(const std::string &letter)
{
	q_get_last_incremental_image->Bind(clientid);
	q_get_last_incremental_image->Bind(letter);
	db_results res=q_get_last_incremental_image->Read();
	q_get_last_incremental_image->Reset();
	if(res.size()>0)
	{
		SBackup b;
		b.incremental=watoi(res[0][L"incremental"]);
		b.path=res[0][L"path"];
		b.incremental_ref=watoi(res[0][L"id"]);
		return b;
	}
	else
	{
		SBackup b;
		b.incremental=-2;
		b.incremental_ref=0;
		return b;
	}
}

int BackupServerGet::createBackupSQL(int incremental, int clientid, std::wstring path)
{
	q_create_backup->Bind(incremental);
	q_create_backup->Bind(clientid);
	q_create_backup->Bind(path);
	q_create_backup->Write();
	q_create_backup->Reset();
	return (int)db->getLastInsertID();
}

int BackupServerGet::createBackupImageSQL(int incremental, int incremental_ref, int clientid, std::wstring path, std::string letter)
{
	q_create_backup_image->Bind(clientid);
	q_create_backup_image->Bind(path);
	q_create_backup_image->Bind(incremental);
	q_create_backup_image->Bind(incremental_ref);
	q_create_backup_image->Bind(letter);
	q_create_backup_image->Write();
	q_create_backup_image->Reset();
	return (int)db->getLastInsertID();
}

void BackupServerGet::updateLastseen(void)
{
	q_update_lastseen->Bind(clientid);
	q_update_lastseen->Write();
	q_update_lastseen->Reset();
}

bool BackupServerGet::isUpdateFull(void)
{
	if( server_settings->getSettings()->update_freq_full<0 )
		return false;

	q_update_full->Bind(clientid);
	db_results res=q_update_full->Read();
	q_update_full->Reset();
	return res.empty();
}

bool BackupServerGet::isUpdateIncr(void)
{
	if( server_settings->getSettings()->update_freq_incr<0 )
		return false;

	q_update_incr->Bind(clientid);
	db_results res=q_update_incr->Read();
	q_update_incr->Reset();
	return res.empty();
}

bool BackupServerGet::isUpdateFullImage(const std::string &letter)
{
	if( server_settings->getSettings()->update_freq_image_full<0 )
		return false;

	q_update_image_full->Bind(clientid);
	q_update_image_full->Bind(letter);
	db_results res=q_update_image_full->Read();
	q_update_image_full->Reset();
	return res.empty();
}

bool BackupServerGet::isUpdateFullImage(void)
{
	std::vector<std::string> vols=server_settings->getBackupVolumes();
	for(size_t i=0;i<vols.size();++i)
	{
		if( isUpdateFullImage(vols[i]+":") )
		{
			return true;
		}
	}
	return false;
}

bool BackupServerGet::isUpdateIncrImage(void)
{
	std::vector<std::string> vols=server_settings->getBackupVolumes();
	for(size_t i=0;i<vols.size();++i)
	{
		if( isUpdateIncrImage(vols[i]+":") )
		{
			return true;
		}
	}
	return false;
}

bool BackupServerGet::isUpdateIncrImage(const std::string &letter)
{
	if( server_settings->getSettings()->update_freq_image_full<0 || server_settings->getSettings()->update_freq_image_incr<0 )
		return false;

	q_update_image_incr->Bind(clientid);
	q_update_image_incr->Bind(letter);
	db_results res=q_update_image_incr->Read();
	q_update_image_incr->Reset();
	return res.empty();
}

void BackupServerGet::updateRunning(bool image)
{
	if(image)
	{
		q_update_running_image->Bind(backupid);
		q_update_running_image->Write();
		q_update_running_image->Reset();
	}
	else
	{
		q_update_running_file->Bind(backupid);
		q_update_running_file->Write();
		q_update_running_file->Reset();
	}
}

void BackupServerGet::saveImageAssociation(int image_id, int assoc_id)
{
	q_save_image_assoc->Bind(image_id);
	q_save_image_assoc->Bind(assoc_id);
	q_save_image_assoc->Write();
	q_save_image_assoc->Reset();
}

bool BackupServerGet::getNextEntry(char ch, SFile &data, std::map<std::wstring, std::wstring>* extra)
{
	switch(state)
	{
	case 0:
		if(ch=='f')
			data.isdir=false;
		else if(ch=='d')
			data.isdir=true;
		else
			ServerLogger::Log(clientid, "Error parsing file BackupServerGet::getNextEntry - 1", LL_ERROR);
		state=1;
		break;
	case 1:
		//"
		state=2;
		break;
	case 3:
		if(ch!='"')
		{
			t_name.erase(t_name.size()-1,1);
			data.name=Server->ConvertToUnicode(t_name);
			t_name="";
			if(data.isdir)
			{
				resetEntryState();
				return true;
			}
			else
				state=4;
		}
	case 2:
		if(state==2 && ch=='"')
			state=3;
		else if(state==3)
			state=2;
		
		t_name+=ch;
		break;
	case 4:
		if(ch!=' ')
		{
			t_name+=ch;
		}
		else
		{
			data.size=os_atoi64(t_name);
			t_name="";
			state=5;
		}
		break;
	case 5:
		if(ch!='\n' && ch!='#')
		{
			t_name+=ch;
		}
		else
		{
			data.last_modified=os_atoi64(t_name);
			if(ch=='\n')
			{
				resetEntryState();
				return true;
			}
			else
			{
				t_name="";
				state=6;
			}
		}
		break;
	case 6:
		if(ch!='\n')
		{
			t_name+=ch;
		}
		else
		{
			if(extra!=NULL)
			{
				ParseParamStr(t_name, extra, false);
			}
			resetEntryState();
			return true;
		}
		break;
	}
	return false;
}

void BackupServerGet::resetEntryState(void)
{
	t_name="";
	state=0;
}

bool BackupServerGet::request_filelist_construct(bool full, bool with_token, bool *no_backup_dirs)
{
	if(server_settings->getSettings()->end_to_end_file_backup_verification)
	{
		sendClientMessage("ENABLE END TO END FILE BACKUP VERIFICATION", "OK", L"Enabling end to end file backup verficiation on client failed.", 10000);
	}

	unsigned int timeout_time=full_backup_construct_timeout;
	if(file_protocol_version>=2)
	{
		timeout_time=120000;
	}

	CTCPStack tcpstack(internet_connection);

	ServerLogger::Log(clientid, clientname+L": Connecting for filelist...", LL_DEBUG);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed - CONNECT error during filelist construction", LL_ERROR);
		return false;
	}

	std::string pver="";
	if(file_protocol_version==2) pver="2";

	std::string start_backup_cmd=server_identity+pver;

	if(full)
	{
		start_backup_cmd+="START FULL BACKUP";
	}
	else
	{
		start_backup_cmd+="START BACKUP";
	}

	if(with_token)
	{
		start_backup_cmd+="#token="+server_token;
	}

	tcpstack.Send(cc, start_backup_cmd);

	ServerLogger::Log(clientid, clientname+L": Waiting for filelist", LL_DEBUG);
	std::string ret;
	unsigned int starttime=Server->getTimeMS();
	while(Server->getTimeMS()-starttime<=timeout_time)
	{
		size_t rc=cc->Read(&ret, 60000);
		if(rc==0)
		{			
			if(file_protocol_version<2 && Server->getTimeMS()-starttime<=20000 && with_token==true) //Compatibility with older clients
			{
				Server->destroy(cc);
				ServerLogger::Log(clientid, clientname+L": Trying old filelist request", LL_WARNING);
				return request_filelist_construct(full, false, no_backup_dirs);
			}
			else
			{
				if(file_protocol_version>=2 || pingthread->isTimeout() )
				{
					ServerLogger::Log(clientid, L"Constructing of filelist of \""+clientname+L"\" failed - TIMEOUT(1)", LL_ERROR);
					break;
				}
				else
				{
					continue;
				}
			}
		}
		tcpstack.AddData((char*)ret.c_str(), ret.size());

		size_t packetsize;
		char *pck=tcpstack.getPacket(&packetsize);
		if(pck!=NULL && packetsize>0)
		{
			ret=pck;
			delete [] pck;
			if(ret!="DONE")
			{
				if(ret=="BUSY")
				{
					starttime=Server->getTimeMS();
				}
				else if(ret!="no backup dirs")
				{
					logVssLogdata();
					ServerLogger::Log(clientid, L"Constructing of filelist of \""+clientname+L"\" failed: "+widen(ret), LL_ERROR);
					break;
				}
				else
				{
					ServerLogger::Log(clientid, L"Constructing of filelist of \""+clientname+L"\" failed: "+widen(ret)+L". Please add paths to backup on the client (via tray icon) or configure default paths to backup.", LL_ERROR);
					if(no_backup_dirs!=NULL)
					{
						*no_backup_dirs=true;
					}
					break;
				}				
			}
			else
			{
				logVssLogdata();
				Server->destroy(cc);
				return true;
			}
		}
	}
	Server->destroy(cc);
	return false;
}

bool BackupServerGet::doFullBackup(bool with_hashes, bool &disk_error, bool &log_backup)
{
	if(!handle_not_enough_space(L""))
		return false;
	
	bool no_backup_dirs=false;
	bool b=request_filelist_construct(true, true, &no_backup_dirs);
	if(!b)
	{
		has_error=true;

		if(no_backup_dirs)
		{
			log_backup=false;
		}
		else
		{
			log_backup=true;
		}

		return false;
	}

	bool hashed_transfer=true;

	if(internet_connection)
	{
		if(server_settings->getSettings()->internet_full_file_transfer_mode=="raw")
			hashed_transfer=false;
	}
	else
	{
		if(server_settings->getSettings()->local_full_file_transfer_mode=="raw")
			hashed_transfer=false;
	}

	if(hashed_transfer)
	{
		ServerLogger::Log(clientid, clientname+L": Doing backup with hashed transfer...", LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(clientid, clientname+L": Doing backup without hashed transfer...", LL_DEBUG);
	}

	FileClient fc(false, filesrv_protocol_version, internet_connection, this, use_tmpfiles?NULL:this);
	_u32 rc=getClientFilesrvConnection(&fc, 10000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, L"Full Backup of "+clientname+L" failed - CONNECT error", LL_ERROR);
		has_error=true;
		log_backup=false;
		return false;
	}
	
	IFile *tmp=getTemporaryFileRetry();;
	if(tmp==NULL) 
	{
		ServerLogger::Log(clientid, L"Error creating temporary file in ::doFullBackup", LL_ERROR);
		return false;
	}

	unsigned int full_backup_starttime=Server->getTimeMS();

	rc=fc.GetFile("urbackup/filelist.ub", tmp, hashed_transfer);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting filelist of "+clientname+L". Errorcode: "+widen(fc.getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		has_error=true;
		return false;
	}

	backupid=createBackupSQL(0, clientid, backuppath_single);
	
	tmp->Seek(0);
	
	resetEntryState();

	IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+"_new.ub", MODE_WRITE);

	if(clientlist==NULL )
	{
		ServerLogger::Log(clientid, L"Error creating clientlist for client "+clientname, LL_ERROR);
		has_error=true;
		return false;
	}

	if(ServerStatus::isBackupStopped(clientname))
	{
		ServerLogger::Log(clientid, L"Server admin stopped backup. -1", LL_ERROR);
		has_error=true;
		return false;
	}

	_i64 filelist_size=tmp->Size();

	char buffer[4096];
	_u32 read;
	_i64 filelist_currpos=0;
	std::wstring curr_path;
	std::wstring curr_os_path;
	SFile cf;
	int depth=0;
	bool r_done=false;
	unsigned int laststatsupdate=0;
	ServerStatus::setServerStatus(status, true);
	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, false);
	Server->getThreadPool()->execute(running_updater);

	std::vector<size_t> diffs;
	_i64 files_size=getIncrementalSize(tmp, diffs, true);
	_i64 transferred=0;

	tmp->Seek(0);
	
	bool c_has_error=false;

	while( (read=tmp->Read(buffer, 4096))>0 && r_done==false && c_has_error==false)
	{
		if(ServerStatus::isBackupStopped(clientname))
		{
			r_done=true;
			ServerLogger::Log(clientid, L"Server admin stopped backup.", LL_ERROR);
			break;
		}
		filelist_currpos+=read;
		for(size_t i=0;i<read;++i)
		{
			unsigned int ctime=Server->getTimeMS();
			if(ctime-laststatsupdate>status_update_intervall)
			{
				laststatsupdate=ctime;
				if(files_size==0)
				{
					status.pcdone=100;
				}
				else
				{
					status.pcdone=(std::min)(100,(int)(((float)transferred)/((float)files_size/100.f)+0.5f));
				}
				status.hashqueuesize=(_u32)hashpipe->getNumElements();
				status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements();
				ServerStatus::setServerStatus(status, true);
			}
			std::map<std::wstring, std::wstring> extra_params;
			bool b=getNextEntry(buffer[i], cf, &extra_params);
			if(b)
			{
				std::wstring short_name=shortenFilename(cf.name);
				if(cf.isdir==true)
				{
					if(cf.name!=L"..")
					{
						curr_path+=L"/"+cf.name;
						curr_os_path+=L"/"+short_name;
						std::wstring local_curr_os_path=convertToOSPathFromFileClient(curr_os_path);

						if(!os_create_dir(os_file_prefix(backuppath+local_curr_os_path)))
						{
							ServerLogger::Log(clientid, L"Creating directory  \""+backuppath+local_curr_os_path+L"\" failed.", LL_ERROR);
							c_has_error=true;
							break;
						}
						if(with_hashes && !os_create_dir(os_file_prefix(backuppath_hashes+local_curr_os_path)))
						{
							ServerLogger::Log(clientid, L"Creating directory  \""+backuppath_hashes+local_curr_os_path+L"\" failed.", LL_ERROR);
							c_has_error=true;
							break;
						}
						++depth;
						if(depth==1)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							ServerLogger::Log(clientid, L"Starting shadowcopy \""+t+L"\".", LL_INFO);
							start_shadowcopy(Server->ConvertToUTF8(t));
							Server->wait(10000);
						}
					}
					else
					{
						--depth;
						if(depth==0)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							ServerLogger::Log(clientid, L"Stoping shadowcopy \""+t+L"\".", LL_INFO);
							stop_shadowcopy(Server->ConvertToUTF8(t));
						}
						curr_path=ExtractFilePath(curr_path);
						curr_os_path=ExtractFilePath(curr_os_path);
					}
					writeFileRepeat(clientlist, "d\""+Server->ConvertToUTF8(cf.name)+"\"\n");
				}
				else
				{
					bool file_ok=false;
					std::map<std::wstring, std::wstring>::iterator hash_it=( (local_hash==NULL)?extra_params.end():extra_params.find(L"sha512") );
					if( hash_it!=extra_params.end())
					{
						if(link_file(cf.name, short_name, curr_path, curr_os_path, with_hashes, base64_decode(wnarrow(hash_it->second)), cf.size, true))
						{
							transferred+=cf.size;
							file_ok=true;
						}
					}
					if(!file_ok)
					{
						bool b=load_file(cf.name, short_name, curr_path, curr_os_path, fc, with_hashes, L"", L"", file_ok, hashed_transfer);
						if(!b)
						{
							ServerLogger::Log(clientid, L"Client "+clientname+L" went offline.", LL_ERROR);
							r_done=true;
							break;
						}
						
						transferred+=cf.size;
					}
					if( file_ok )
					{
						writeFileRepeat(clientlist, "f\""+Server->ConvertToUTF8(cf.name)+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
					}
				}
			}
		}

		if(read<4096)
			break;
	}
	if(r_done==false && c_has_error==false)
	{
		sendBackupOkay(true);
	}
	else
	{
		sendBackupOkay(false);
	}


	running_updater->stop();
	updateRunning(false);
	Server->destroy(clientlist);

	waitForFileThreads();

	if( bsh->hasError() || bsh_prepare->hasError() )
	{
		disk_error=true;
	}
	else
	{
		db->BeginTransaction();
		if(!os_rename_file(L"urbackup/clientlist_"+convert(clientid)+L"_new.ub", L"urbackup/clientlist_"+convert(clientid)+L".ub") )
		{
			ServerLogger::Log(clientid, "Renaming new client file list to destination failed", LL_ERROR);
		}
		setBackupDone();
		db->EndTransaction();
	}

	if( r_done==false && c_has_error==false && disk_error==false) 
	{
		std::wstring backupfolder=server_settings->getSettings()->backupfolder;
		std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+L"current";
		os_remove_symlink_dir(os_file_prefix(currdir));
		os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

		currdir=backupfolder+os_file_sep()+L"clients";
		if(!os_create_dir(os_file_prefix(currdir)) && !os_directory_exists(os_file_prefix(currdir)))
		{
			Server->Log("Error creating \"clients\" dir for symbolic links", LL_ERROR);
		}
		currdir+=os_file_sep()+clientname;
		Server->deleteFile(os_file_prefix(currdir));
		os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

		if(server_settings->getSettings()->end_to_end_file_backup_verification && !verify_file_backup(tmp))
		{
			ServerLogger::Log(clientid, "Backup verification failed", LL_ERROR);
			c_has_error=true;
		}
	}

	{
		std::wstring tmp_fn=tmp->getFilenameW();
		Server->destroy(tmp);
		Server->deleteFile(os_file_prefix(tmp_fn));
	}

	_i64 transferred_bytes=fc.getTransferredBytes();
	unsigned int passed_time=Server->getTimeMS()-full_backup_starttime;
	if(passed_time==0) passed_time=1;

	ServerLogger::Log(clientid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );
	
	if(c_has_error)
		return false;

	return !r_done;
}

bool BackupServerGet::load_file_patch(const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path,
	const std::wstring &last_backuppath, const std::wstring &last_backuppath_complete, FileClientChunked &fc, FileClient &fc_normal, bool &download_ok)
{
	std::wstring cfn=curr_path+L"/"+fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	std::wstring cfn_short=os_path+L"/"+short_fn;
	if(cfn_short[0]=='/')
		cfn_short.erase(0,1);

	std::wstring dstpath=backuppath+os_file_sep()+convertToOSPathFromFileClient(cfn_short);
	std::wstring hashpath=backuppath_hashes+os_file_sep()+convertToOSPathFromFileClient(cfn_short);
	std::wstring hashpath_old=last_backuppath+os_file_sep()+L".hashes"+os_file_sep()+convertToOSPathFromFileClient(cfn_short);
	std::wstring filepath_old=last_backuppath+os_file_sep()+convertToOSPathFromFileClient(cfn_short);

	std::auto_ptr<IFile> file_old(Server->openFile(os_file_prefix(filepath_old), MODE_READ));

	if(file_old.get()==NULL)
	{
		if(!last_backuppath_complete.empty())
		{
			filepath_old=last_backuppath_complete+os_file_sep()+convertToOSPathFromFileClient(cfn_short);
			file_old.reset(Server->openFile(os_file_prefix(filepath_old), MODE_READ));
		}
		if(file_old.get()==NULL)
		{
			ServerLogger::Log(clientid, L"No old file for \""+fn+L"\"", LL_DEBUG);
			return load_file(fn, short_fn, curr_path, os_path, fc_normal, true, last_backuppath, last_backuppath_complete, download_ok, true);
		}
		hashpath_old=last_backuppath_complete+os_file_sep()+L".hashes"+os_file_sep()+convertToOSPathFromFileClient(cfn_short);
	}

	ServerLogger::Log(clientid, L"Loading file patch for \""+fn+L"\"", LL_DEBUG);
	IFile *pfd=getTemporaryFileRetry();
	if(pfd==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
		return false;
	}
	ScopedDeleteFile pfd_destroy(pfd);
	IFile *hash_tmp=getTemporaryFileRetry();
	if(hash_tmp==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'hash_tmp' in load_file_patch", LL_ERROR);
		return false;
	}
	ScopedDeleteFile hash_tmp_destroy(hash_tmp);

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}

	std::auto_ptr<IFile> hashfile_old(Server->openFile(os_file_prefix(hashpath_old), MODE_READ));
	
	bool delete_hashfile=false;

	ScopedDeleteFile hashfile_old_destroy(NULL);

	if( (hashfile_old.get()==NULL || hashfile_old->Size()==0 ) && file_old.get()!=NULL )
	{
		ServerLogger::Log(clientid, L"Hashes for file \""+filepath_old+L"\" not available. Calulating hashes...", LL_DEBUG);
		hashfile_old.reset(getTemporaryFileRetry());
		if(hashfile_old.get()==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'hashfile_old' in load_file_patch", LL_ERROR);
			return false;
		}
		hashfile_old_destroy.reset(hashfile_old.get());
		delete_hashfile=true;
		BackupServerPrepareHash::build_chunk_hashs(file_old.get(), hashfile_old.get(), NULL, false, NULL, false);
		hashfile_old->Seek(0);
	}

	_u32 rc=fc.GetFilePatch(Server->ConvertToUTF8(cfn), file_old.get(), pfd, hashfile_old.get(), hash_tmp);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		file_old->Seek(0);
		pfd=getTemporaryFileRetry();
		if(pfd==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'pfd' in load_file_patch", LL_ERROR);
			return false;
		}
		pfd_destroy.reset(pfd);
		hash_tmp=getTemporaryFileRetry();
		if(hash_tmp==NULL)
		{
			ServerLogger::Log(clientid, L"Error creating temporary file 'hash_tmp' in load_file_patch -2", LL_ERROR);
			return false;
		}
		hash_tmp_destroy.reset(hash_tmp);
		hashfile_old->Seek(0);
		rc=fc.GetFilePatch(Server->ConvertToUTF8(cfn), file_old.get(), pfd, hashfile_old.get(), hash_tmp);
		--hash_retries;
	} 

	if(rc!=ERR_SUCCESS)
	{
		download_ok=false;

		ServerLogger::Log(clientid, L"Error getting file \""+cfn+L"\" from "+clientname+L". Errorcode: "+widen(FileClient::getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
	}
	else
	{
		download_ok=true;

		std::wstring os_curr_path=convertToOSPathFromFileClient(os_path+L"/"+short_fn);		
		std::wstring dstpath=backuppath+os_curr_path;

		pfd_destroy.release();
		hash_tmp_destroy.release();
		hashFile(dstpath, hashpath, pfd, hash_tmp, Server->ConvertToUTF8(filepath_old));
	}

	if(delete_hashfile)
	{
		hashfile_old.release();
	}
	else
	{
		hashfile_old_destroy.release();
	}

	if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_SOCKET_ERROR
		|| rc==ERR_INT_ERROR || rc==ERR_BASE_DIR_LOST || rc==ERR_CONN_LOST )
		return false;
	else
		return true;
}

bool BackupServerGet::link_file(const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path, bool with_hashes, const std::string& sha2, _i64 filesize, bool add_sql)
{
	std::wstring os_curr_path=convertToOSPathFromFileClient(os_path+L"/"+short_fn);
	std::wstring dstpath=backuppath+os_curr_path;
	std::wstring hashpath;
	std::wstring filepath_old;
	if(with_hashes)
	{
		hashpath=backuppath_hashes+os_curr_path;
	}

	bool tries_once;
	std::wstring ff_last;
	bool ok=local_hash->findFileAndLink(dstpath, NULL, hashpath, sha2, true, filesize, std::string(), tries_once, ff_last);

	if(ok && add_sql)
	{
		local_hash->addFileSQL(backupid, 0, dstpath, hashpath, sha2, filesize, 0);
		local_hash->copyFromTmpTable(false);
	}
	
	return ok;
}

bool BackupServerGet::load_file(const std::wstring &fn, const std::wstring &short_fn, const std::wstring &curr_path, const std::wstring &os_path,
	FileClient &fc, bool with_hashes, const std::wstring &last_backuppath, const std::wstring &last_backuppath_complete, bool &download_ok, bool hashed_transfer)
{
	ServerLogger::Log(clientid, L"Loading file \""+fn+L"\"", LL_DEBUG);
	IFile *fd=getTemporaryFileRetry();
	if(fd==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file 'fd' in load_file", LL_ERROR);
		return false;
	}

	std::wstring cfn=curr_path+L"/"+fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}
	
	_u32 rc=fc.GetFile(Server->ConvertToUTF8(cfn), fd, hashed_transfer);

	int hash_retries=5;
	while(rc==ERR_HASH && hash_retries>0)
	{
		fd->Seek(0);
		rc=fc.GetFile(Server->ConvertToUTF8(cfn), fd, hashed_transfer);
		--hash_retries;
	}

	if(rc!=ERR_SUCCESS)
	{
		download_ok=false;
		ServerLogger::Log(clientid, L"Error getting file \""+cfn+L"\" from "+clientname+L". Errorcode: "+widen(fc.getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		destroyTemporaryFile(fd);
		if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_BASE_DIR_LOST)
			return false;
	}
	else
	{
		download_ok=true;

		std::wstring os_curr_path=convertToOSPathFromFileClient(os_path+L"/"+short_fn);		
		std::wstring dstpath=backuppath+os_curr_path;
		std::wstring hashpath;
		std::wstring filepath_old;
		if(with_hashes)
		{
			hashpath=backuppath_hashes+os_curr_path;
		}
		if( use_reflink && (!last_backuppath.empty() || !last_backuppath_complete.empty() ) )
		{
			std::wstring cfn_short=os_path+L"/"+short_fn;
			if(cfn_short[0]=='/')
				cfn_short.erase(0,1);

			filepath_old=last_backuppath+os_file_sep()+convertToOSPathFromFileClient(cfn_short);

			IFile *file_old=Server->openFile(os_file_prefix(filepath_old), MODE_READ);

			if(file_old==NULL)
			{
				if(!last_backuppath_complete.empty())
				{
					filepath_old=last_backuppath_complete+os_file_sep()+convertToOSPathFromFileClient(cfn_short);
					file_old=Server->openFile(os_file_prefix(filepath_old), MODE_READ);
				}
				if(file_old==NULL)
				{
					ServerLogger::Log(clientid, L"No old file for \""+fn+L"\"", LL_DEBUG);
					filepath_old.clear();
				}
			}

			if(file_old!=NULL)
			{
				Server->destroy(file_old);
			}
		}

		hashFile(dstpath, hashpath, fd, NULL, Server->ConvertToUTF8(filepath_old));
	}
	return true;
}

_i64 BackupServerGet::getIncrementalSize(IFile *f, const std::vector<size_t> &diffs, bool all)
{
	f->Seek(0);
	_i64 rsize=0;
	resetEntryState();
	SFile cf;
	bool indirchange=false;
	size_t read;
	size_t line=0;
	char buffer[4096];
	int indir_currdepth=0;
	int depth=0;
	int indir_curr_depth=0;
	int changelevel=0;

	if(all)
	{
		indirchange=true;
	}

	while( (read=f->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			bool b=getNextEntry(buffer[i], cf, NULL);
			if(b)
			{
				if(cf.isdir==true)
				{
					if(indirchange==false && hasChange(line, diffs) )
					{
						indirchange=true;
						changelevel=depth;
						indir_currdepth=0;
					}
					else if(indirchange==true)
					{
						if(cf.name!=L"..")
							++indir_currdepth;
						else
							--indir_currdepth;
					}

					if(cf.name==L".." && indir_currdepth>0)
					{
						--indir_currdepth;
					}

					if(cf.name!=L"..")
					{
						++depth;
					}
					else
					{
						--depth;
						if(indirchange==true && depth==changelevel)
						{
							if(!all)
							{
								indirchange=false;
							}
						}
					}
				}
				else
				{
					if(indirchange==true || hasChange(line, diffs))
					{
						rsize+=cf.size;
					}
				}
				++line;
			}
		}

		if(read<4096)
			break;
	}

	return rsize;
}

bool BackupServerGet::doIncrBackup(bool with_hashes, bool intra_file_diffs, bool on_snapshot, bool &disk_error, bool &log_backup)
{
	int64 free_space=os_free_space(os_file_prefix(server_settings->getSettings()->backupfolder));
	if(free_space!=-1 && free_space<minfreespace_min)
	{
		ServerLogger::Log(clientid, "No space for directory entries. Freeing space", LL_WARNING);

		if(!ServerCleanupThread::cleanupSpace(minfreespace_min) )
		{
			ServerLogger::Log(clientid, "Could not free space for directory entries. NOT ENOUGH FREE SPACE.", LL_ERROR);
			return false;
		}
	}

	if(with_hashes)
	{
		ServerLogger::Log(clientid, clientname+L": Doing backup with hashes...", LL_DEBUG);
	}

	if(intra_file_diffs)
	{
		ServerLogger::Log(clientid, clientname+L": Doing backup with intra file diffs...", LL_DEBUG);
	}

	SBackup last=getLastIncremental();
	if(last.incremental==-2)
	{
		ServerLogger::Log(clientid, "Cannot retrieve last file backup when doing incremental backup. Doing full backup now...", LL_WARNING);

		if(on_snapshot)
		{
			bool b=SnapshotHelper::createEmptyFilesystem(clientname, backuppath_single)  && (!with_hashes || os_create_dir(os_file_prefix(backuppath_hashes)));

			if(!b)
			{
				ServerLogger::Log(clientid, "Cannot filesystem for backup", LL_ERROR);
				return false;
			}
		}

		return doFullBackup(with_hashes, disk_error, log_backup);
	}
	
	bool no_backup_dirs=false;
	bool b=request_filelist_construct(false, true, &no_backup_dirs);
	if(!b)
	{
		has_error=true;

		if(no_backup_dirs)
		{
			log_backup=false;
		}
		else
		{
			log_backup=true;
		}

		return false;
	}

	bool hashed_transfer=true;

	if(internet_connection)
	{
		if(server_settings->getSettings()->internet_incr_file_transfer_mode=="raw")
			hashed_transfer=false;
	}
	else
	{
		if(server_settings->getSettings()->local_incr_file_transfer_mode=="raw")
			hashed_transfer=false;
	}

	if(hashed_transfer)
	{
		ServerLogger::Log(clientid, clientname+L": Doing backup with hashed transfer...", LL_DEBUG);
	}
	else
	{
		ServerLogger::Log(clientid, clientname+L": Doing backup without hashed transfer...", LL_DEBUG);
	}

	Server->Log(clientname+L": Connecting to client...", LL_DEBUG);
	FileClient fc(false, filesrv_protocol_version, internet_connection, this, use_tmpfiles?NULL:this);
	FileClientChunked fc_chunked;
	if(intra_file_diffs)
	{
		fc_chunked=getClientChunkedFilesrvConnection(10000);
		fc_chunked.setDestroyPipe(true);
		if(fc_chunked.hasError())
		{
			ServerLogger::Log(clientid, L"Incremental Backup of "+clientname+L" failed - CONNECT error -1", LL_ERROR);
			has_error=true;
			log_backup=false;
			return false;
		}
	}
	_u32 rc=getClientFilesrvConnection(&fc, 10000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, L"Incremental Backup of "+clientname+L" failed - CONNECT error -2", LL_ERROR);
		has_error=true;
		log_backup=false;
		return false;
	}
	
	ServerLogger::Log(clientid, clientname+L": Loading filelist...", LL_DEBUG);
	IFile *tmp=getTemporaryFileRetry();
	if(tmp==NULL)
	{
		ServerLogger::Log(clientid, L"Error creating temporary file in ::doIncrBackup", LL_ERROR);
		return false;
	}

	unsigned int incr_backup_starttime=Server->getTimeMS();
	unsigned int incr_backup_stoptime=0;

	rc=fc.GetFile("urbackup/filelist.ub", tmp, hashed_transfer);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting filelist of "+clientname+L". Errorcode: "+widen(fc.getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		has_error=true;
		return false;
	}
	
	ServerLogger::Log(clientid, clientname+L" Starting incremental backup...", LL_DEBUG);

	
	backupid=createBackupSQL(last.incremental+1, clientid, backuppath_single);

	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	std::wstring last_backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path;
	std::wstring last_backuppath_hashes=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path+os_file_sep()+L".hashes";
	std::wstring last_backuppath_complete=backupfolder+os_file_sep()+clientname+os_file_sep()+last.complete;

	std::wstring tmpfilename=tmp->getFilenameW();
	Server->destroy(tmp);

	ServerLogger::Log(clientid, clientname+L": Calculating file tree differences...", LL_DEBUG);

	bool error=false;
	std::vector<size_t> deleted_ids;
	std::vector<size_t> *deleted_ids_ref=NULL;
	if(on_snapshot) deleted_ids_ref=&deleted_ids;

	std::vector<size_t> diffs=TreeDiff::diffTrees("urbackup/clientlist_"+nconvert(clientid)+".ub", wnarrow(tmpfilename), error, deleted_ids_ref);

	if(error)
	{
		if(!internet_connection)
		{
			ServerLogger::Log(clientid, "Error while calculating tree diff. Doing full backup.", LL_ERROR);
			return doFullBackup(with_hashes, disk_error, log_backup);
		}
		else
		{
			ServerLogger::Log(clientid, "Error while calculating tree diff. Not doing full backup because of internet connection.", LL_ERROR);
			has_error=true;
			return false;
		}
	}

	if(on_snapshot)
	{
		ServerLogger::Log(clientid, clientname+L": Creating snapshot...", LL_DEBUG);
		if(!SnapshotHelper::snapshotFileSystem(clientname, last.path, backuppath_single)
			|| !SnapshotHelper::isSubvolume(clientname, backuppath_single) )
		{
			ServerLogger::Log(clientid, "Creating new snapshot failed (Server error)", LL_WARNING);
			
			if(!SnapshotHelper::createEmptyFilesystem(clientname, backuppath_single) )
			{
				ServerLogger::Log(clientid, "Creating empty filesystem failed (Server error)", LL_ERROR);
				has_error=true;
				return false;
			}
			if(with_hashes)
			{
				if(!os_create_dir(os_file_prefix(backuppath_hashes)) )
				{
					ServerLogger::Log(clientid, "Cannot create hash path (Server error)", LL_ERROR);
					has_error=true;
					return false;
				}
			}
			
			on_snapshot=false;
		}
		
		if(on_snapshot)
		{
			ServerLogger::Log(clientid, clientname+L": Deleting files in snapshot... ("+convert(deleted_ids.size())+L")", LL_DEBUG);
			if(!deleteFilesInSnapshot("urbackup/clientlist_"+nconvert(clientid)+".ub", deleted_ids, backuppath, false) )
			{
				ServerLogger::Log(clientid, "Deleting files in snapshot failed (Server error)", LL_ERROR);
				has_error=true;
				return false;
			}

			if(with_hashes)
			{
				ServerLogger::Log(clientid, clientname+L": Deleting files in hash snapshot...", LL_DEBUG);
				deleteFilesInSnapshot("urbackup/clientlist_"+nconvert(clientid)+".ub", deleted_ids, backuppath_hashes, true);
			}
		}
	}

	IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+"_new.ub", MODE_WRITE);

	tmp=Server->openFile(tmpfilename, MODE_READ);

	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, false);
	Server->getThreadPool()->execute(running_updater);
	
	char buffer[4096];
	_u32 read;
	std::wstring curr_path;
	std::wstring curr_os_path;
	std::wstring curr_hash_path;
	SFile cf;
	int depth=0;
	int line=0;
	link_logcnt=0;
	bool indirchange=false;
	int changelevel;
	bool r_offline=false;
	_i64 filelist_size=tmp->Size();
	_i64 filelist_currpos=0;
	int indir_currdepth=0;
	
	ServerLogger::Log(clientid, clientname+L": Calculating tree difference size...", LL_DEBUG);
	_i64 files_size=getIncrementalSize(tmp, diffs);
	tmp->Seek(0);
	_i64 transferred=0;
	
	unsigned int laststatsupdate=0;
	ServerStatus::setServerStatus(status, true);
	
	ServerLogger::Log(clientid, clientname+L": Linking unchanged and loading new files...", LL_DEBUG);

	resetEntryState();
	
	bool c_has_error=false;
	bool backup_stopped=false;

	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		if(!backup_stopped)
		{
			if(ServerStatus::isBackupStopped(clientname))
			{
				r_offline=true;
				backup_stopped=true;
				ServerLogger::Log(clientid, L"Server admin stopped backup.", LL_ERROR);
			}
		}

		filelist_currpos+=read;

		for(size_t i=0;i<read;++i)
		{
			std::map<std::wstring, std::wstring> extra_params;
			bool b=getNextEntry(buffer[i], cf, &extra_params);
			if(b)
			{
				unsigned int ctime=Server->getTimeMS();
				if(ctime-laststatsupdate>status_update_intervall)
				{
					laststatsupdate=ctime;
					if(files_size==0)
					{
						status.pcdone=100;
					}
					else
					{
						status.pcdone=(std::min)(100,(int)(((float)transferred)/((float)files_size/100.f)+0.5f));
					}
					status.hashqueuesize=(_u32)hashpipe->getNumElements();
					status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements();
					ServerStatus::setServerStatus(status, true);
				}

				std::wstring short_name=shortenFilename(cf.name);				
				if(cf.isdir==true)
				{
					if(indirchange==false && hasChange(line, diffs) )
					{
						indirchange=true;
						changelevel=depth;
						indir_currdepth=0;

						if(cf.name!=L"..")
						{
							if(r_offline==false )
								indir_currdepth=1;
						}
						else
						{
							--changelevel;
						}
					}
					else if(indirchange==true && r_offline==false)
					{
						if(cf.name!=L"..")
							++indir_currdepth;
						else
							--indir_currdepth;
					}

					if(indirchange==false || r_offline==false )
					{
						writeFileRepeat(clientlist, "d\""+Server->ConvertToUTF8(cf.name)+"\"\n");
					}
					else if(cf.name==L".." && indir_currdepth>0)
					{
						--indir_currdepth;
						writeFileRepeat(clientlist, "d\"..\"\n");
					}

					if(cf.name!=L"..")
					{
						curr_path+=L"/"+cf.name;
						curr_os_path+=L"/"+short_name;
						std::wstring local_curr_os_path=convertToOSPathFromFileClient(curr_os_path);

						if(!on_snapshot || (indirchange && !r_offline) )
						{
							if(!os_create_dir(os_file_prefix(backuppath+local_curr_os_path)))
							{
								if(!os_directory_exists(os_file_prefix(backuppath+local_curr_os_path)))
								{
									ServerLogger::Log(clientid, L"Creating directory  \""+backuppath+local_curr_os_path+L"\" failed.", LL_ERROR);
									c_has_error=true;
									break;
								}
								else
								{
									ServerLogger::Log(clientid, L"Directory \""+backuppath+local_curr_os_path+L"\" does already exist.", LL_WARNING);
								}
							}
							if(with_hashes && !os_create_dir(os_file_prefix(backuppath_hashes+local_curr_os_path)))
							{
								if(!os_directory_exists(os_file_prefix(backuppath_hashes+local_curr_os_path)))
								{
									ServerLogger::Log(clientid, L"Creating directory  \""+backuppath_hashes+local_curr_os_path+L"\" failed.", LL_ERROR);
									c_has_error=true;
									break;
								}
								else
								{
									ServerLogger::Log(clientid, L"Directory  \""+backuppath_hashes+local_curr_os_path+L"\" does already exist.", LL_WARNING);
								}
							}
						}
						++depth;
						if(depth==1)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							if(r_offline==false)
							{
								start_shadowcopy(Server->ConvertToUTF8(t));
							}
						}
					}
					else
					{
						--depth;
						if(indirchange==true && depth==changelevel)
						{
							indirchange=false;
						}
						if(depth==0)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							if(r_offline==false)
							{
								stop_shadowcopy(Server->ConvertToUTF8(t));
							}
						}
						curr_path=ExtractFilePath(curr_path);
						curr_os_path=ExtractFilePath(curr_os_path);
					}
				}
				else //is file
				{
					std::wstring local_curr_os_path=convertToOSPathFromFileClient(curr_os_path+L"/"+short_name);
					
					bool f_ok=false;

					if(indirchange==true || hasChange(line, diffs)) //is changed
					{
						std::map<std::wstring, std::wstring>::iterator hash_it=( (local_hash==NULL)?extra_params.end():extra_params.find(L"sha512") );

						if(r_offline==false && hash_it!=extra_params.end())
						{
							if(link_file(cf.name, short_name, curr_path, curr_os_path, with_hashes, base64_decode(wnarrow(hash_it->second)), cf.size, false))
							{
								transferred+=cf.size;
								f_ok=true;
							}
						}

						if(r_offline==false && !f_ok)
						{
							bool b;
							bool download_ok;
							if(intra_file_diffs)
								b=load_file_patch(cf.name, short_name, curr_path, curr_os_path, last_backuppath, last_backuppath_complete, fc_chunked, fc, download_ok);
							else
								b=load_file(cf.name, short_name, curr_path, curr_os_path, fc, with_hashes, last_backuppath, last_backuppath_complete, download_ok, hashed_transfer);

							if(!b)
							{
								ServerLogger::Log(clientid, L"Client "+clientname+L" went offline.", LL_ERROR);
								r_offline=true;
								incr_backup_stoptime=Server->getTimeMS();
							}
							else
							{
								if(download_ok)
								{
									transferred+=cf.size;
									f_ok=true;
								}
							}
						}
					}
					else if(!on_snapshot) //is not changed
					{			
						std::wstring srcpath=last_backuppath+local_curr_os_path;
						bool too_many_hardlinks;
						bool b=os_create_hardlink(os_file_prefix(backuppath+local_curr_os_path), os_file_prefix(srcpath), use_snapshots, &too_many_hardlinks);
						if(b)
						{
							f_ok=true;
						}
						else if(!b && too_many_hardlinks)
						{
							ServerLogger::Log(clientid, L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Hardlink limit was reached. Copying file...", LL_DEBUG);
							copyFile(srcpath, backuppath+local_curr_os_path);
							f_ok=true;
						}

						if(!f_ok)
						{
							if(link_logcnt<5)
							{
								ServerLogger::Log(clientid, L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Loading file...", LL_WARNING);
							}
							else if(link_logcnt==5)
							{
								ServerLogger::Log(clientid, L"More warnings of kind: Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Loading file... Skipping.", LL_WARNING);
							}
							else
							{
								Server->Log(L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+local_curr_os_path+L"\" failed. Loading file...", LL_WARNING);
							}
							++link_logcnt;

							std::map<std::wstring, std::wstring>::iterator hash_it=( (local_hash==NULL)?extra_params.end():extra_params.find(L"sha512") );

							if(r_offline==false && hash_it!=extra_params.end())
							{
								if(link_file(cf.name, short_name, curr_path, curr_os_path, with_hashes, base64_decode(wnarrow(hash_it->second)), cf.size, false))
								{
									f_ok=true;
								}
							}

							if(r_offline==false && !f_ok)
							{
								bool download_ok;
								bool b2;
								if(intra_file_diffs)
									b2=load_file_patch(cf.name, short_name, curr_path, curr_os_path, last_backuppath, last_backuppath_complete, fc_chunked, fc, download_ok);
								else
									b2=load_file(cf.name, short_name, curr_path, curr_os_path, fc, with_hashes, last_backuppath, last_backuppath_complete, download_ok, hashed_transfer);

								if(!b2)
								{
									ServerLogger::Log(clientid, L"Client "+clientname+L" went offline.", LL_ERROR);
									r_offline=true;
									incr_backup_stoptime=Server->getTimeMS();
								}
								else
								{
									if(download_ok)
									{
										f_ok=true;
									}
								}
							}
						}
						else if(with_hashes)
						{
							os_create_hardlink(os_file_prefix(backuppath_hashes+local_curr_os_path), os_file_prefix(last_backuppath_hashes+local_curr_os_path), use_snapshots, NULL);
						}
					}
					else
					{
						f_ok=true;
					}
					
					if(f_ok)
					{
						writeFileRepeat(clientlist, "f\""+Server->ConvertToUTF8(cf.name)+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
					}
				}
				++line;
			}
		}
		
		if(c_has_error)
			break;

		if(read<4096)
			break;
	}
	status.pcdone=100;
	ServerStatus::setServerStatus(status, true);
	Server->destroy(clientlist);

	sendBackupOkay(r_offline==false && c_has_error==false);

	waitForFileThreads();

	if( bsh->hasError() || bsh_prepare->hasError() )
	{
		disk_error=true;
	}
	
	if(r_offline==false && c_has_error==false && disk_error==false)
	{
		std::wstring dst_file=L"urbackup/clientlist_"+convert(clientid)+L"_new.ub";

		db->BeginTransaction();
		bool b=os_rename_file(dst_file, L"urbackup/clientlist_"+convert(clientid)+L".ub");
		if(b)
		{
			setBackupDone();
		}
		db->EndTransaction();

		if(b && server_settings->getSettings()->end_to_end_file_backup_verification && !verify_file_backup(tmp))
		{
			ServerLogger::Log(clientid, "Backup verification failed", LL_ERROR);
			c_has_error=true;
		}

		if(b)
		{
			ServerLogger::Log(clientid, "Creating symbolic links. -1", LL_DEBUG);

			std::wstring backupfolder=server_settings->getSettings()->backupfolder;
			std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+L"current";

			os_remove_symlink_dir(os_file_prefix(currdir));		
			os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

			ServerLogger::Log(clientid, "Creating symbolic links. -2", LL_DEBUG);

			currdir=backupfolder+os_file_sep()+L"clients";
			if(!os_create_dir(os_file_prefix(currdir)) && !os_directory_exists(os_file_prefix(currdir)))
			{
				ServerLogger::Log(clientid, "Error creating \"clients\" dir for symbolic links", LL_ERROR);
			}
			currdir+=os_file_sep()+clientname;
			os_remove_symlink_dir(os_file_prefix(currdir));
			os_link_symbolic(os_file_prefix(backuppath), os_file_prefix(currdir));

			ServerLogger::Log(clientid, "Symbolic links created.", LL_DEBUG);
		}
		else
		{
			ServerLogger::Log(clientid, "Fatal error renaming clientlist.", LL_ERROR);
			sendMailToAdmins("Fatal error occured during incremental file backup", ServerLogger::getWarningLevelTextLogdata(clientid));
		}
	}
	else if(!c_has_error && !disk_error)
	{
		ServerLogger::Log(clientid, "Client disconnected while backing up. Copying partial file...", LL_DEBUG);
		db->BeginTransaction();
		moveFile(L"urbackup/clientlist_"+convert(clientid)+L"_new.ub", L"urbackup/clientlist_"+convert(clientid)+L".ub");
		setBackupDone();
		db->EndTransaction();
	}
	else
	{
		ServerLogger::Log(clientid, "Fatal error during backup. Backup not completed", LL_ERROR);
		sendMailToAdmins("Fatal error occured during incremental file backup", ServerLogger::getWarningLevelTextLogdata(clientid));
	}

	running_updater->stop();
	updateRunning(false);
	Server->destroy(tmp);
	Server->deleteFile(tmpfilename);

	if(incr_backup_stoptime==0)
	{
		incr_backup_stoptime=Server->getTimeMS();
	}

	_i64 transferred_bytes=fc.getTransferredBytes()+fc_chunked.getTransferredBytes();
	unsigned int passed_time=incr_backup_stoptime-incr_backup_starttime;
	ServerLogger::Log(clientid, "Transferred "+PrettyPrintBytes(transferred_bytes)+" - Average speed: "+PrettyPrintSpeed((size_t)((transferred_bytes*1000)/(passed_time)) ), LL_INFO );

	if(c_has_error) return false;
	
	return !r_offline;
}

void BackupServerGet::sendBackupOkay(bool b_okay)
{
	if(b_okay)
	{
		notifyClientBackupSuccessfull();
	}
	else
	{
		if(pingthread!=NULL)
		{
			pingthread->setStop(true);
			Server->getThreadPool()->waitFor(pingthread_ticket);
		}
		pingthread=NULL;
	}
}

void BackupServerGet::waitForFileThreads(void)
{
	SStatus status=ServerStatus::getStatus(clientname);
	hashpipe->Write("flush");
	hashpipe_prepare->Write("flush");
	status.hashqueuesize=(_u32)hashpipe->getNumElements()+(bsh->isWorking()?1:0);
	status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements()+(bsh_prepare->isWorking()?1:0);
	while(status.hashqueuesize>0 || status.prepare_hashqueuesize>0)
	{
		ServerStatus::setServerStatus(status, true);
		Server->wait(1000);
		status.hashqueuesize=(_u32)hashpipe->getNumElements()+(bsh->isWorking()?1:0);
		status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements()+(bsh_prepare->isWorking()?1:0);
	}
	{
		Server->wait(10);
		while(bsh->isWorking()) Server->wait(1000);
	}	
}

bool BackupServerGet::deleteFilesInSnapshot(const std::string clientlist_fn, const std::vector<size_t> &deleted_ids, std::wstring snapshot_path, bool no_error)
{
	resetEntryState();

	IFile *tmp=Server->openFile(clientlist_fn, MODE_READ);
	if(tmp==NULL)
	{
		ServerLogger::Log(clientid, "Could not open clientlist in ::deleteFilesInSnapshot", LL_ERROR);
		return false;
	}

	char buffer[4096];
	size_t read;
	SFile curr_file;
	size_t line=0;
	std::wstring curr_path=snapshot_path;
	std::wstring curr_os_path=snapshot_path;
	bool curr_dir_exists=true;

	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			if(getNextEntry(buffer[i], curr_file, NULL))
			{
				if(curr_file.isdir)
				{
					if(curr_file.name==L"..")
					{
						curr_path=ExtractFilePath(curr_path);
						curr_os_path=ExtractFilePath(curr_os_path);
						if(!curr_dir_exists)
						{
							curr_dir_exists=os_directory_exists(curr_path);
						}
					}
				}

				if( hasChange(line, deleted_ids) )
				{
					std::wstring short_fn=shortenFilename(curr_file.name);
					std::wstring curr_fn=convertToOSPathFromFileClient(curr_os_path+os_file_sep()+short_fn);
					if(curr_file.isdir)
					{
						if(curr_dir_exists)
						{
							if(!os_remove_nonempty_dir(curr_fn) )
							{
								if(!no_error)
								{
									ServerLogger::Log(clientid, L"Could not remove directory \""+curr_fn+L"\" in ::deleteFilesInSnapshot", LL_ERROR);
									Server->destroy(tmp);
									return false;
								}
							}
						}
						curr_path+=os_file_sep()+curr_file.name;
						curr_os_path+=os_file_sep()+short_fn;
						curr_dir_exists=false;
					}
					else
					{
						if( curr_dir_exists )
						{
							if( !Server->deleteFile(curr_fn) )
							{
								if(!no_error)
								{
									ServerLogger::Log(clientid, L"Could not remove file \""+curr_fn+L"\" in ::deleteFilesInSnapshot", LL_ERROR);
									Server->destroy(tmp);
									return false;
								}
							}
						}
					}
				}
				else if( curr_file.isdir && curr_file.name!=L".." )
				{
					curr_path+=os_file_sep()+curr_file.name;
					curr_os_path+=os_file_sep()+shortenFilename(curr_file.name);
				}
				++line;
			}
		}
	}

	Server->destroy(tmp);
	return true;
}

bool BackupServerGet::hasChange(size_t line, const std::vector<size_t> &diffs)
{
	return std::binary_search(diffs.begin(), diffs.end(), line);
}

void BackupServerGet::hashFile(std::wstring dstpath, std::wstring hashpath, IFile *fd, IFile *hashoutput, std::string old_file)
{
	int l_backup_id=backupid;

	CWData data;
	data.addString(Server->ConvertToUTF8(fd->getFilenameW()));
	data.addInt(l_backup_id);
	data.addChar(r_incremental==true?1:0);
	data.addString(Server->ConvertToUTF8(dstpath));
	data.addString(Server->ConvertToUTF8(hashpath));
	if(hashoutput!=NULL)
	{
		data.addString(Server->ConvertToUTF8(hashoutput->getFilenameW()));
	}
	else
	{
		data.addString("");
	}

	data.addString(old_file);

	ServerLogger::Log(clientid, "GT: Loaded file \""+ExtractFileName(Server->ConvertToUTF8(dstpath))+"\"", LL_DEBUG);

	Server->destroy(fd);
	if(hashoutput!=NULL)
	{
		Server->destroy(hashoutput);
	}
	hashpipe_prepare->Write(data.getDataPtr(), data.getDataSize() );
}

bool BackupServerGet::constructBackupPath(bool with_hashes, bool on_snapshot, bool create_fs)
{
	time_t tt=time(NULL);
#ifdef _WIN32
	tm lt;
	tm *t=&lt;
	localtime_s(t, &tt);
#else
	tm *t=localtime(&tt);
#endif
	char buffer[500];
	strftime(buffer, 500, "%y%m%d-%H%M", t);
	backuppath_single=widen((std::string)buffer);
	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath_single;
	if(with_hashes)
		backuppath_hashes=backupfolder+os_file_sep()+clientname+os_file_sep()+backuppath_single+os_file_sep()+L".hashes";
	else
		backuppath_hashes.clear();

	if(on_snapshot)
	{
		if(create_fs)
		{
			return SnapshotHelper::createEmptyFilesystem(clientname, backuppath_single)  && (!with_hashes || os_create_dir(os_file_prefix(backuppath_hashes)));
		}
		else
		{
			return true;
		}
	}
	else
	{
		return os_create_dir(os_file_prefix(backuppath)) && (!with_hashes || os_create_dir(os_file_prefix(backuppath_hashes)));	
	}
}

std::wstring BackupServerGet::constructImagePath(const std::wstring &letter)
{
	time_t tt=time(NULL);
#ifdef _WIN32
	tm lt;
	tm *t=&lt;
	localtime_s(t, &tt);
#else
	tm *t=localtime(&tt);
#endif
	char buffer[500];
	strftime(buffer, 500, "%y%m%d-%H%M", t);
	std::wstring backupfolder_uncompr=server_settings->getSettings()->backupfolder_uncompr;
	return backupfolder_uncompr+os_file_sep()+clientname+os_file_sep()+L"Image_"+letter+L"_"+widen((std::string)buffer)+L".vhd";
}

void BackupServerGet::updateLastBackup(void)
{
	q_set_last_backup->Bind(backupid);
	q_set_last_backup->Bind(clientid);
	q_set_last_backup->Write();
	q_set_last_backup->Reset();
}

void BackupServerGet::updateLastImageBackup(void)
{
	q_set_last_image_backup->Bind(backupid);
	q_set_last_image_backup->Bind(clientid);
	q_set_last_image_backup->Write();
	q_set_last_image_backup->Reset();
}

std::string BackupServerGet::sendClientMessage(const std::string &msg, const std::wstring &errmsg, unsigned int timeout, bool logerr, int max_loglevel)
{
	CTCPStack tcpstack(internet_connection);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		if(logerr)
			ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, max_loglevel);
		else
			Server->Log(L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, max_loglevel);
		return "";
	}

	tcpstack.Send(cc, server_identity+msg);

	std::string ret;
	unsigned int starttime=Server->getTimeMS();
	bool ok=false;
	bool herr=false;
	while(Server->getTimeMS()-starttime<=timeout)
	{
		size_t rc=cc->Read(&ret, timeout);
		if(rc==0)
		{
			if(logerr)
				ServerLogger::Log(clientid, errmsg, max_loglevel);
			else
				Server->Log(errmsg, max_loglevel);

			break;
		}
		tcpstack.AddData((char*)ret.c_str(), ret.size());

		size_t packetsize;
		char *pck=tcpstack.getPacket(&packetsize);
		if(pck!=NULL && packetsize>0)
		{
			ret.resize(packetsize);
			memcpy(&ret[0], pck, packetsize);
			delete [] pck;
			Server->destroy(cc);
			return ret;
		}
	}

	if(logerr)
		ServerLogger::Log(clientid, L"Timeout: "+errmsg, max_loglevel);
	else
		Server->Log(L"Timeout: "+errmsg, max_loglevel);

	Server->destroy(cc);

	return "";
}

bool BackupServerGet::sendClientMessage(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, bool logerr, int max_loglevel, bool *retok_err)
{
	CTCPStack tcpstack(internet_connection);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		if(logerr)
			ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, max_loglevel);
		else
			Server->Log(L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, max_loglevel);
		return false;
	}

	tcpstack.Send(cc, server_identity+msg);

	std::string ret;
	unsigned int starttime=Server->getTimeMS();
	bool ok=false;
	bool herr=false;
	while(Server->getTimeMS()-starttime<=timeout)
	{
		size_t rc=cc->Read(&ret, timeout);
		if(rc==0)
		{
			break;
		}
		tcpstack.AddData((char*)ret.c_str(), ret.size());

		size_t packetsize;
		char *pck=tcpstack.getPacket(&packetsize);
		if(pck!=NULL && packetsize>0)
		{
			ret=pck;
			delete [] pck;
			if(ret!=retok)
			{
				herr=true;
				if(logerr)
					ServerLogger::Log(clientid, errmsg, max_loglevel);
				else

				if(retok_err!=NULL)
					*retok_err=true;

				break;
			}
			else
			{
				ok=true;
				break;
			}
		}
		else if(pck!=NULL)
		{
			delete []pck;
		}
	}
	if(!ok && !herr)
	{
		if(logerr)
			ServerLogger::Log(clientid, L"Timeout: "+errmsg, max_loglevel);
		else
			Server->Log(L"Timeout: "+errmsg, max_loglevel);
	}

	Server->destroy(cc);

	return ok;
}

void BackupServerGet::start_shadowcopy(const std::string &path)
{
	sendClientMessage("START SC \""+path+"\"#token="+server_token, "DONE", L"Activating shadow copy on \""+clientname+L"\" for path \""+Server->ConvertToUnicode(path)+L"\" failed", shadow_copy_timeout);
}

void BackupServerGet::stop_shadowcopy(const std::string &path)
{
	sendClientMessage("STOP SC \""+path+"\"#token="+server_token, "DONE", L"Removing shadow copy on \""+clientname+L"\" for path \""+Server->ConvertToUnicode(path)+L"\" failed", shadow_copy_timeout);
}

void BackupServerGet::notifyClientBackupSuccessfull(void)
{
	sendClientMessage("DID BACKUP", "OK", L"Sending status (DID BACKUP) to client failed", 10000);
}

void BackupServerGet::sendClientBackupIncrIntervall(void)
{
	sendClientMessage("INCRINTERVALL \""+nconvert(server_settings->getSettings()->update_freq_incr)+"\"", "OK", L"Sending incremental file backup interval to client failed", 10000);
}

bool BackupServerGet::updateCapabilities(void)
{
	std::string cap=sendClientMessage("CAPA", L"Querying client capabilities failed", 10000, false);
	if(cap!="ERR" && !cap.empty())
	{
		str_map params;
		ParseParamStr(cap, &params);
		if(params[L"IMAGE"]!=L"1")
		{
			Server->Log("Client doesn't have IMAGE capability", LL_DEBUG);
			can_backup_images=false;
		}
		str_map::iterator it=params.find(L"FILESRV");
		if(it!=params.end())
		{
			filesrv_protocol_version=watoi(it->second);
		}

		it=params.find(L"FILE");
		if(it!=params.end())
		{
			file_protocol_version=watoi(it->second);
		}	

		it=params.find(L"SET_SETTINGS");
		if(it!=params.end())
		{
			set_settings_version=watoi(it->second);
		}
		it=params.find(L"IMAGE_VER");
		if(it!=params.end())
		{
			image_protocol_version=watoi(it->second);
		}
		it=params.find(L"CLIENTUPDATE");
		if(it!=params.end())
		{
			update_version=watoi(it->second);
		}
		it=params.find(L"CLIENT_VERSION_STR");
		if(it!=params.end())
		{
			ServerStatus::setClientVersionString(clientname, Server->ConvertToUTF8(it->second));
		}
		it=params.find(L"OS_VERSION_STR");
		if(it!=params.end())
		{
			ServerStatus::setOSVersionString(clientname, Server->ConvertToUTF8(it->second));
		}
	}

	return !cap.empty();
}

void BackupServerGet::sendSettings(void)
{
	std::string s_settings;

	std::vector<std::wstring> settings_names=getSettingsList();
	std::vector<std::wstring> global_settings_names=getGlobalizedSettingsList();
	std::vector<std::wstring> local_settings_names=getLocalizedSettingsList();

	std::string stmp=settings_client->getValue("overwrite", "");
	bool overwrite=true;
	if(!stmp.empty())
		overwrite=(stmp=="true");

	bool allow_overwrite=true;
	stmp=settings_client->getValue("allow_overwrite", "");
	if(stmp.empty())
		stmp=settings->getValue("allow_overwrite", "");
	if(!stmp.empty())
		allow_overwrite=(stmp=="true");
		

	for(size_t i=0;i<settings_names.size();++i)
	{
		std::wstring key=settings_names[i];
		std::wstring value;

		bool globalized=std::find(global_settings_names.begin(), global_settings_names.end(), key)!=global_settings_names.end();
		bool localized=std::find(local_settings_names.begin(), local_settings_names.end(), key)!=local_settings_names.end();

		if( globalized || (!overwrite && !allow_overwrite && !localized) || !settings_client->getValue(key, &value) )
		{
			if(!settings->getValue(key, &value) )
				key=L"";
		}
		
		key+=L"_def";

		if(!key.empty())
		{
			s_settings+=Server->ConvertToUTF8(key)+"="+Server->ConvertToUTF8(value)+"\n";
		}
	}
	escapeClientMessage(s_settings);
	sendClientMessage("SETTINGS "+s_settings, "OK", L"Sending settings to client failed", 10000);
}	

bool BackupServerGet::getClientSettings(void)
{
	FileClient fc(false, filesrv_protocol_version, internet_connection, this, use_tmpfiles?NULL:this);
	_u32 rc=getClientFilesrvConnection(&fc);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, L"Getting Client settings of "+clientname+L" failed - CONNECT error", LL_ERROR);
		return false;
	}
	
	IFile *tmp=getTemporaryFileRetry();
	if(tmp==NULL)
	{
		ServerLogger::Log(clientid, "Error creating temporary file in BackupServerGet::getClientSettings", LL_ERROR);
		return false;
	}
	rc=fc.GetFile("urbackup/settings.cfg", tmp, true);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting Client settings of "+clientname+L". Errorcode: "+widen(fc.getErrorString(rc))+L" ("+convert(rc)+L")", LL_ERROR);
		std::string tmp_fn=tmp->getFilename();
		Server->destroy(tmp);
		Server->deleteFile(tmp_fn);
		return false;
	}

	ISettingsReader *sr=Server->createFileSettingsReader(tmp->getFilename());

	std::vector<std::wstring> setting_names=getSettingsList();

	bool mod=false;

	if(set_settings_version>0)
	{
		std::string tmp_str;
		if(!sr->getValue("client_set_settings", &tmp_str) || tmp_str!="true" )
		{
			Server->destroy(sr);
			std::string tmp_fn=tmp->getFilename();
			Server->destroy(tmp);
			Server->deleteFile(tmp_fn);
			return true;
		}
		else
		{
			bool b=updateClientSetting(L"client_set_settings", L"true");
			if(b)
				mod=true;
		}
	}

	
	for(size_t i=0;i<setting_names.size();++i)
	{
		std::wstring &key=setting_names[i];
		std::wstring value;

		if(internet_connection && key==L"computername")
		{
			continue;
		}

		if(sr->getValue(key, &value) )
		{
			bool b=updateClientSetting(key, value);
			if(b)
				mod=true;
		}
	}

	Server->destroy(sr);

	bool b=updateClientSetting(L"client_overwrite", L"true");
	if(b)
		mod=true;
	
	std::string tmp_fn=tmp->getFilename();
	Server->destroy(tmp);
	Server->deleteFile(tmp_fn);

	if(mod)
	{
		server_settings->update();
		unloadSQL();
		prepareSQL();
	}

	return true;
}

bool BackupServerGet::updateClientSetting(const std::wstring &key, const std::wstring &value)
{
	std::wstring tmp;
	if(settings_client->getValue(key, &tmp)==false )
	{
		q_insert_setting->Bind(key);
		q_insert_setting->Bind(value);
		q_insert_setting->Bind(clientid);
		q_insert_setting->Write();
		q_insert_setting->Reset();
		return true;
	}
	else if(tmp!=value)
	{
		q_update_setting->Bind(value);
		q_update_setting->Bind(key);
		q_update_setting->Bind(clientid);
		q_update_setting->Write();
		q_update_setting->Reset();
		return true;
	}
	return false;
}

void BackupServerGet::setBackupComplete(void)
{
	q_set_complete->Bind(backupid);
	q_set_complete->Write();
	q_set_complete->Reset();
}

void BackupServerGet::setBackupDone(void)
{
	q_set_done->Bind(backupid);
	q_set_done->Write();
	q_set_done->Reset();
}

void BackupServerGet::setBackupImageComplete(void)
{
	q_set_image_complete->Bind(backupid);
	q_set_image_complete->Write();
	q_set_image_complete->Reset();
}

void BackupServerGet::sendToPipe(const std::string &msg)
{
	pipe->Write(msg);
}

int BackupServerGet::getPCDone(void)
{
	SStatus st=ServerStatus::getStatus(clientname);
	if(!st.has_status)
		return 0;
	else
		return st.pcdone;
}

void BackupServerGet::sendClientLogdata(void)
{
	q_get_unsent_logdata->Bind(clientid);
	db_results res=q_get_unsent_logdata->Read();
	q_get_unsent_logdata->Reset();

	for(size_t i=0;i<res.size();++i)
	{
		std::string logdata=Server->ConvertToUTF8(res[i][L"logdata"]);
		escapeClientMessage(logdata);
		sendClientMessage("2LOGDATA "+wnarrow(res[i][L"created"])+" "+logdata, "OK", L"Sending logdata to client failed", 10000, false, LL_WARNING);
		q_set_logdata_sent->Bind(res[i][L"id"]);
		q_set_logdata_sent->Write();
		q_set_logdata_sent->Reset();
	}
}

void BackupServerGet::saveClientLogdata(int image, int incremental, bool r_success)
{
	int errors=0;
	int warnings=0;
	int infos=0;
	std::wstring logdata=ServerLogger::getLogdata(clientid, errors, warnings, infos);

	q_save_logdata->Bind(clientid);
	q_save_logdata->Bind(logdata);
	q_save_logdata->Bind(errors);
	q_save_logdata->Bind(warnings);
	q_save_logdata->Bind(infos);
	q_save_logdata->Bind(image);
	q_save_logdata->Bind(incremental);
	q_save_logdata->Write();
	q_save_logdata->Reset();

	sendLogdataMail(r_success, image, incremental, errors, warnings, infos, logdata);

	ServerLogger::reset(clientid);
}

std::wstring BackupServerGet::getUserRights(int userid, std::string domain)
{
	if(domain!="all")
	{
		if(getUserRights(userid, "all")==L"all")
			return L"all";
	}
	q_get_rights->Bind(userid);
	q_get_rights->Bind(domain);
	db_results res=q_get_rights->Read();
	q_get_rights->Reset();
	if(!res.empty())
	{
		return res[0][L"t_right"];
	}
	else
	{
		return L"none";
	}
}

void BackupServerGet::sendLogdataMail(bool r_success, int image, int incremental, int errors, int warnings, int infos, std::wstring &data)
{
	MailServer mail_server=getMailServerSettings();
	if(mail_server.servername.empty())
		return;

	if(url_fak==NULL)
		return;

	db_results res_users=q_get_users->Read();
	q_get_users->Reset();
	for(size_t i=0;i<res_users.size();++i)
	{
		std::wstring logr=getUserRights(watoi(res_users[i][L"id"]), "logs");
		bool has_r=false;
		if(logr!=L"all")
		{
			std::vector<std::wstring> toks;
			Tokenize(logr, toks, L",");
			for(size_t j=0;j<toks.size();++j)
			{
				if(watoi(toks[j])==clientid)
				{
					has_r=true;
				}
			}
		}
		else
		{
			has_r=true;
		}

		if(has_r)
		{
			q_get_report_settings->Bind(watoi(res_users[i][L"id"]));
			db_results res=q_get_report_settings->Read();
			q_get_report_settings->Reset();

			if(!res.empty())
			{
				std::wstring report_mail=res[0][L"report_mail"];
				int report_loglevel=watoi(res[0][L"report_loglevel"]);
				int report_sendonly=watoi(res[0][L"report_sendonly"]);

				if( ( ( report_loglevel==0 && infos>0 )
					|| ( report_loglevel<=1 && warnings>0 )
					|| ( report_loglevel<=2 && errors>0 ) ) &&
					(report_sendonly==0 ||
						( report_sendonly==1 && !r_success ) ||
						( report_sendonly==2 && r_success)) )
				{
					std::vector<std::string> to_addrs;
					Tokenize(Server->ConvertToUTF8(report_mail), to_addrs, ",;");

					std::string subj="UrBackup: ";
					std::string msg="UrBackup just did ";
					if(incremental>0)
					{
						msg+="an incremental ";
						subj="Incremental ";
					}
					else
					{
						msg+="a full ";
						subj="Full ";
					}

					if(image>0)
					{
						msg+="image ";
						subj+="image ";
					}
					else
					{
						msg+="file ";
						subj+="file ";
					}
					subj+="backup of \""+Server->ConvertToUTF8(clientname)+"\"\n";
					msg+="backup of \""+Server->ConvertToUTF8(clientname)+"\".\n";
					msg+="\nReport:\n";
					msg+="( "+nconvert(infos);
					if(infos!=1) msg+=" infos, ";
					else msg+=" info, ";
					msg+=nconvert(warnings);
					if(warnings!=1) msg+=" warnings, ";
					else msg+=" warning, ";
					msg+=nconvert(errors);
					if(errors!=1) msg+=" errors";
					else msg+=" error";
					msg+=" )\n\n";
					std::vector<std::wstring> msgs;
					TokenizeMail(data, msgs, L"\n");
					
					for(size_t j=0;j<msgs.size();++j)
					{
						std::wstring ll;
						if(!msgs[j].empty()) ll=msgs[j][0];
						int li=watoi(ll);
						msgs[j].erase(0, 2);
						std::wstring tt=getuntil(L"-", msgs[j]);
						std::wstring m=getafter(L"-", msgs[j]);

						q_format_unixtime->Bind(tt);
						db_results ft=q_format_unixtime->Read();
						q_format_unixtime->Reset();
						if( !ft.empty() )
						{
							tt=ft[0][L"time"];
						}
						std::string lls="info";
						if(li==1) lls="warning";
						else if(li==2) lls="error";
						msg+=Server->ConvertToUTF8(tt)+"("+lls+"): "+Server->ConvertToUTF8(m)+"\n";
					}
					if(!r_success)
						subj+=" - failed";
					else
						subj+=" - success";

					std::string errmsg;
					bool b=url_fak->sendMail(mail_server, to_addrs, subj, msg, &errmsg);
					if(!b)
					{
						Server->Log("Sending mail failed. "+errmsg, LL_WARNING);
					}
				}
			}
		}
	}
}

MailServer BackupServerGet::getMailServerSettings(void)
{
	ISettingsReader *settings=Server->createDBSettingsReader(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER), "settings_db.settings", "SELECT value FROM settings WHERE key=? AND clientid=0");

	MailServer ms;
	ms.servername=settings->getValue("mail_servername", "");
	ms.port=(unsigned short)watoi(settings->getValue(L"mail_serverport", L"587"));
	ms.username=settings->getValue("mail_username", "");
	ms.password=settings->getValue("mail_password", "");
	ms.mailfrom=settings->getValue("mail_from", "");
	if(ms.mailfrom.empty())
		ms.mailfrom="report@urbackup.org";

	ms.ssl_only=(settings->getValue("mail_ssl_only", "false")=="true")?true:false;
	ms.check_certificate=(settings->getValue("mail_check_certificate", "false")=="true")?true:false;

	Server->destroy(settings);
	return ms;
}

bool BackupServerGet::sendMailToAdmins(const std::string& subj, const std::string& message)
{
	MailServer mail_server=getMailServerSettings();
	if(mail_server.servername.empty())
		return false;

	if(url_fak==NULL)
		return false;

	ISettingsReader *settings=Server->createDBSettingsReader(Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER), "settings_db.settings", "SELECT value FROM settings WHERE key=? AND clientid=0");
	std::string admin_addrs_str=settings->getValue("mail_admin_addrs", "");

	std::vector<std::string> admin_addrs;
	Tokenize(admin_addrs_str, admin_addrs, ";,");

	std::string errmsg;
	bool b=url_fak->sendMail(mail_server, admin_addrs, "[UrBackup] "+subj, message, &errmsg);
	if(!b)
	{
		Server->Log("Sending mail failed. "+errmsg, LL_WARNING);	
		return false;
	}
	return true;
}

std::string BackupServerGet::getMBR(const std::wstring &dl)
{
	std::string ret=sendClientMessage("MBR driveletter="+wnarrow(dl), L"Getting MBR for drive "+dl+L" failed", 10000);
	CRData r(&ret);
	char b;
	if(r.getChar(&b) && b==1 )
	{
		char ver;
		if(r.getChar(&ver) )
		{
			if(ver!=0)
			{
				ServerLogger::Log(clientid, L"Server version does not fit", LL_ERROR);
			}
			else
			{
				CRData r2(&ret);
				SMBRData mbrdata(r2);
				if(!mbrdata.errmsg.empty())
				{
					ServerLogger::Log(clientid, "During getting MBR: "+mbrdata.errmsg, LL_WARNING);
				}
				return ret;
			}
		}
		else
		{
			ServerLogger::Log(clientid, L"Could not read version information in MBR", LL_ERROR);
		}
	}
	else if(dl!=L"SYSVOL")
	{
		std::string errmsg;
		if( r.getStr(&errmsg) && !errmsg.empty())
		{
			errmsg=". Error message: "+errmsg;
		}
		ServerLogger::Log(clientid, "Could not read MBR"+errmsg, LL_ERROR);
	}

	return "";
}

void BackupServerGet::checkClientVersion(void)
{
	std::string version=getFile("urbackup/version.txt");
	if(!version.empty())
	{
		std::string r=sendClientMessage("VERSION "+version, L"Sending version to client failed", 10000);
		if(r=="update")
		{
			IFile *sigfile=Server->openFile("urbackup/UrBackupUpdate.sig", MODE_READ);
			if(sigfile==NULL)
			{
				ServerLogger::Log(clientid, "Error opening sigfile", LL_ERROR);
				return;
			}
			IFile *updatefile=Server->openFile("urbackup/UrBackupUpdate.exe", MODE_READ);
			if(updatefile==NULL)
			{
				ServerLogger::Log(clientid, "Error opening updatefile", LL_ERROR);
				return;
			}			
			size_t datasize=3*sizeof(unsigned int)+version.size()+(size_t)sigfile->Size()+(size_t)updatefile->Size();

			CTCPStack tcpstack(internet_connection);
			IPipe *cc=getClientCommandConnection(10000);
			if(cc==NULL)
			{
				ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed - CONNECT error", LL_ERROR);
				return;
			}

			std::string msg;
			if(update_version>0)
			{
				msg="1CLIENTUPDATE size="+nconvert(datasize)+"&silent_update="+nconvert(server_settings->getSettings()->silent_update);
			}
			else
			{
				msg="CLIENTUPDATE "+nconvert(datasize);
			}
			tcpstack.Send(cc, server_identity+msg);

			int timeout=5*60*1000;

			unsigned int c_size=(unsigned int)version.size();
			if(!cc->Write((char*)&c_size, sizeof(unsigned int), timeout) )
			{
				Server->destroy(cc);
				Server->destroy(sigfile);
				Server->destroy(updatefile);
				return;
			}
			if(!cc->Write(version, timeout) )
			{
				Server->destroy(cc);
				Server->destroy(sigfile);
				Server->destroy(updatefile);
				return;
			}
			c_size=(unsigned int)sigfile->Size();
			if(!cc->Write((char*)&c_size, sizeof(unsigned int), timeout) )
			{
				Server->destroy(cc);
				Server->destroy(sigfile);
				Server->destroy(updatefile);
				return;
			}
			if(!sendFile(cc, sigfile, timeout) )
			{
				Server->destroy(cc);
				Server->destroy(sigfile);
				Server->destroy(updatefile);
				return;
			}
			c_size=(unsigned int)updatefile->Size();
			if(!cc->Write((char*)&c_size, sizeof(unsigned int), timeout) )
			{
				Server->destroy(cc);
				Server->destroy(sigfile);
				Server->destroy(updatefile);
				return;
			}
			if(!sendFile(cc, updatefile, timeout) )
			{
				Server->destroy(cc);
				Server->destroy(sigfile);
				Server->destroy(updatefile);
				return;
			}

			Server->destroy(sigfile);
			Server->destroy(updatefile);


			std::string ret;
			unsigned int starttime=Server->getTimeMS();
			bool ok=false;
			while(Server->getTimeMS()-starttime<=5*60*1000)
			{
				size_t rc=cc->Read(&ret, timeout);
				if(rc==0)
				{
					ServerLogger::Log(clientid, "Reading from client failed in update", LL_ERROR);
					break;
				}
				tcpstack.AddData((char*)ret.c_str(), ret.size());

				size_t packetsize;
				char *pck=tcpstack.getPacket(&packetsize);
				if(pck!=NULL && packetsize>0)
				{
					ret.resize(packetsize);
					memcpy(&ret[0], pck, packetsize);
					delete [] pck;
					if(ret=="ok")
					{
						ok=true;
						break;
					}
					else
					{
						ok=false;
						ServerLogger::Log(clientid, "Error in update: "+ret, LL_ERROR);
						break;
					}
				}
			}

			if(!ok)
			{
				ServerLogger::Log(clientid, L"Timeout: In client update", LL_ERROR);
			}

			Server->destroy(cc);

			if(ok)
			{
				ServerLogger::Log(clientid, L"Updated client successfully", LL_INFO);
			}
		}
	}
}

bool BackupServerGet::sendFile(IPipe *cc, IFile *f, int timeout)
{
	char buf[4096];
	_u32 r;
	while((r=f->Read(buf, 4096))>0)
	{
		if(!cc->Write(buf, r, timeout))
			return false;
	}
	return true;
}

sockaddr_in BackupServerGet::getClientaddr(void)
{
	IScopedLock lock(clientaddr_mutex);
	return clientaddr;
}

std::string BackupServerGet::remLeadingZeros(std::string t)
{
	std::string r;
	bool in=false;
	for(size_t i=0;i<t.size();++i)
	{
		if(!in && t[i]!='0' )
			in=true;

		if(in)
		{
			r+=t[i];
		}
	}
	return r;
}

bool BackupServerGet::isInBackupWindow(std::vector<STimeSpan> bw)
{
	if(bw.empty()) return true;
	int dow=atoi(os_strftime("%w").c_str());
	if(dow==0) dow=7;
	
	float hm=(float)atoi(remLeadingZeros(os_strftime("%H")).c_str())+(float)atoi(remLeadingZeros(os_strftime("%M")).c_str())*(1.f/60.f);
	for(size_t i=0;i<bw.size();++i)
	{
		if(bw[i].dayofweek==dow)
		{
			if( (bw[i].start_hour<=bw[i].stop_hour && hm>=bw[i].start_hour && hm<=bw[i].stop_hour)
				|| (bw[i].start_hour>bw[i].stop_hour && (hm>=bw[i].start_hour || hm<=bw[i].stop_hour) ) )
			{
				return true;
			}
		}
	}

	return false;
}

bool BackupServerGet::isBackupsRunningOkay(bool incr, bool file)
{
	IScopedLock lock(running_backup_mutex);
	if(running_backups<server_settings->getSettings()->max_sim_backups)
	{
		if(incr)
		{
			++running_backups;
			if(file)
			{
				++running_file_backups;
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

void BackupServerGet::startBackupRunning(bool file)
{
	IScopedLock lock(running_backup_mutex);
	++running_backups;
	if(file)
	{
		++running_file_backups;
	}
}

void BackupServerGet::stopBackupRunning(bool file)
{
	IScopedLock lock(running_backup_mutex);
	--running_backups;
	if(file)
	{
		--running_file_backups;
	}
}

int BackupServerGet::getNumberOfRunningBackups(void)
{
	IScopedLock lock(running_backup_mutex);
	return running_backups;
}

int BackupServerGet::getNumberOfRunningFileBackups(void)
{
	IScopedLock lock(running_backup_mutex);
	return running_file_backups;
}

void BackupServerGet::writeFileRepeat(IFile *f, const std::string &str)
{
	writeFileRepeat(f, str.c_str(), str.size());
}

void BackupServerGet::writeFileRepeat(IFile *f, const char *buf, size_t bsize)
{
	_u32 written=0;
	do
	{
		_u32 rc=f->Write(buf+written, (_u32)(bsize-written));
		written+=rc;
		if(rc==0)
		{
			Server->Log("Failed to write to file "+f->getFilename()+" retrying...", LL_WARNING);
			Server->wait(10000);
		}
	}
	while(written<bsize );
}

IPipeThrottler *BackupServerGet::getThrottler(size_t speed_bps)
{
	if(client_throttler==NULL)
	{
		client_throttler=Server->createPipeThrottler(speed_bps);
	}
	else
	{
		client_throttler->changeThrottleLimit(speed_bps);
	}

	return client_throttler;
}

IPipe *BackupServerGet::getClientCommandConnection(int timeoutms)
{
	if(internet_connection)
	{
		IPipe *ret=InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_COMMANDS, timeoutms);
		if(server_settings!=NULL && ret!=NULL)
		{
			int internet_speed=server_settings->getSettings()->internet_speed;
			if(internet_speed>0)
			{
				ret->addThrottler(getThrottler(internet_speed));
			}
			int global_internet_speed=server_settings->getSettings()->global_internet_speed;
			if(global_internet_speed>0)
			{
				ret->addThrottler(BackupServer::getGlobalInternetThrottler(global_internet_speed));
			}
		}
		return ret;
	}
	else
	{
		IPipe *ret=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, timeoutms);
		if(server_settings!=NULL && ret!=NULL)
		{
			int local_speed=server_settings->getSettings()->local_speed;
			if(local_speed>0)
			{
				ret->addThrottler(getThrottler(local_speed));
			}
			int global_local_speed=server_settings->getSettings()->global_local_speed;
			if(global_local_speed>0)
			{
				ret->addThrottler(BackupServer::getGlobalLocalThrottler(global_local_speed));
			}
		}
		return ret;
	}
}

_u32 BackupServerGet::getClientFilesrvConnection(FileClient *fc, int timeoutms)
{
	if(internet_connection)
	{
		IPipe *cp=InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_FILESRV, timeoutms);

		_u32 ret=fc->Connect(cp);

		if(server_settings!=NULL)
		{
			int internet_speed=server_settings->getSettings()->internet_speed;
			if(internet_speed>0)
			{
				fc->addThrottler(getThrottler(internet_speed));
			}
			int global_internet_speed=server_settings->getSettings()->global_internet_speed;
			if(global_internet_speed>0)
			{
				fc->addThrottler(BackupServer::getGlobalInternetThrottler(global_internet_speed));
			}
		}

		fc->setReconnectionTimeout(c_internet_fileclient_timeout);

		return ret;
	}
	else
	{
		sockaddr_in addr=getClientaddr();
		_u32 ret=fc->Connect(&addr);

		if(server_settings!=NULL)
		{
			int local_speed=server_settings->getSettings()->local_speed;
			if(local_speed>0)
			{
				fc->addThrottler(getThrottler(local_speed));
			}
			int global_local_speed=server_settings->getSettings()->global_local_speed;
			if(global_local_speed>0)
			{
				fc->addThrottler(BackupServer::getGlobalLocalThrottler(global_local_speed));
			}
		}

		return ret;
	}
}

FileClientChunked BackupServerGet::getClientChunkedFilesrvConnection(int timeoutms)
{
	FileClientChunked ret;
	if(internet_connection)
	{
		IPipe *cp=InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_FILESRV, timeoutms);
		if(cp!=NULL)
		{
			ret=FileClientChunked(cp, &tcpstack, this, use_tmpfiles?NULL:this);
			ret.setReconnectionTimeout(c_internet_fileclient_timeout);
		}
		else
			ret=FileClientChunked();
	}
	else
	{
		sockaddr_in addr=getClientaddr();
		IPipe *pipe=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), TCP_PORT, timeoutms);
		if(pipe!=NULL)
			ret=FileClientChunked(pipe, &tcpstack, this, use_tmpfiles?NULL:this);
		else
			ret=FileClientChunked();
	}

	if(ret.getPipe()!=NULL && server_settings!=NULL)
	{
		int speed;
		if(internet_connection)
		{
			speed=server_settings->getSettings()->internet_speed;
		}
		else
		{
			speed=server_settings->getSettings()->local_speed;
		}
		if(speed>0)
		{
			ret.addThrottler(getThrottler(speed));
		}

		int global_speed;
		if(internet_connection)
		{
			global_speed=server_settings->getSettings()->global_internet_speed;
		}
		else
		{
			global_speed=server_settings->getSettings()->global_local_speed;
		}
		if(global_speed>0)
		{
			ret.addThrottler(BackupServer::getGlobalLocalThrottler(global_speed));
		}
	}

	return ret;
}

std::wstring BackupServerGet::convertToOSPathFromFileClient(std::wstring path)
{
	if(os_file_sep()!=L"/")
	{
		for(size_t i=0;i<path.size();++i)
			if(path[i]=='/')
				path[i]=os_file_sep()[0];
	}
	return path;
}

IFile *BackupServerGet::getTemporaryFileRetry(void)
{
	int tries=50;
	IFile *pfd=NULL;
	while(pfd==NULL)
	{
		if(use_tmpfiles)
		{
			pfd=Server->openTemporaryFile();
		}
		else
		{
			size_t num;
			{
				IScopedLock lock(tmpfile_mutex);
				num=tmpfile_num++;
			}
			pfd=Server->openFile(tmpfile_path+os_file_sep()+convert(num), MODE_RW_CREATE);
		}

		if(pfd==NULL)
		{
			ServerLogger::Log(clientid, "Error opening temporary file. Retrying...", LL_WARNING);
			--tries;
			if(tries<0)
			{
				return NULL;
			}
			Server->wait(1000);
		}
	}
	return pfd;
}

void BackupServerGet::destroyTemporaryFile(IFile *tmp)
{
	std::wstring fn=tmp->getFilenameW();
	Server->destroy(tmp);
	Server->deleteFile(fn);
}

IPipe * BackupServerGet::new_fileclient_connection(void)
{
	IPipe *rp=NULL;
	if(internet_connection)
	{
		rp=InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_FILESRV, c_filesrv_connect_timeout);
	}
	else
	{
		sockaddr_in addr=getClientaddr();
		rp=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), TCP_PORT, c_filesrv_connect_timeout);
	}
	return rp;
}

std::wstring BackupServerGet::shortenFilename(const std::wstring& fn)
{
	std::wstring ret;
	bool shortened_file=false;
#ifdef _WIN32
	if(fn.size()>=MAX_PATH-15)
	{
		ret=fn;
		ServerLogger::Log(clientid, L"Filename \""+fn+L"\" too long. Shortening it.", LL_WARNING);
		ret.resize(MAX_PATH-15);
		shortened_file=true;
	}
#else
	if(Server->ConvertToUTF8(fn).size()>=NAME_MAX-11)
	{
		ret=fn;
		bool log_msg=true;
		do
		{
			if( log_msg )
			{
				ServerLogger::Log(clientid, L"Filename \""+fn+L"\" too long. Shortening it.", LL_WARNING);
				log_msg=false;
			}
			ret.resize(ret.size()-1);
			shortened_file=true;
		}
		while( Server->ConvertToUTF8(ret).size()>=NAME_MAX-11 );
	}
#endif
	if(shortened_file)
	{
		std::string hex_md5=Server->GenerateHexMD5(fn);
		return ret+widen(hex_md5.substr(0, 10));
	}
	else
	{
		return fn;
	}
}

bool BackupServerGet::handle_not_enough_space(const std::wstring &path)
{
	int64 free_space=os_free_space(os_file_prefix(server_settings->getSettings()->backupfolder));
	if(free_space!=-1 && free_space<minfreespace_min)
	{
		Server->Log("No free space in backup folder. Free space="+PrettyPrintBytes(free_space)+" MinFreeSpace="+PrettyPrintBytes(minfreespace_min), LL_WARNING);

		if(!ServerCleanupThread::cleanupSpace(minfreespace_min) )
		{
			ServerLogger::Log(clientid, "FATAL: Could not free space. NOT ENOUGH FREE SPACE.", LL_ERROR);
			sendMailToAdmins("Fatal error occured during backup", ServerLogger::getWarningLevelTextLogdata(clientid));
			return false;
		}
	}

	return true;
}

void BackupServerGet::update_sql_intervals(bool update_sql)
{
	SSettings *settings=server_settings->getSettings();
	if(settings->update_freq_full != curr_intervals.update_freq_full ||
		settings->update_freq_image_full != curr_intervals.update_freq_image_full ||
		settings->update_freq_image_incr != curr_intervals.update_freq_image_incr ||
		settings->update_freq_incr != curr_intervals.update_freq_incr )
	{
		if(update_sql)
		{
			unloadSQL();
			prepareSQL();
		}
	}
	curr_intervals=*settings;
}

bool BackupServerGet::verify_file_backup(IFile *fileentries)
{
	bool verify_ok=true;

	ostringstream log;

	log << "Verification of file backup with id " << backupid << ". Path=" << Server->ConvertToUTF8(backuppath) << std::endl;

	unsigned int read;
	char buffer[4096];
	std::wstring curr_path=backuppath;
	SFile cf;
	fileentries->Seek(0);
	resetEntryState();
	while( (read=fileentries->Read(buffer, 4096))>0 )
	{
		for(size_t i=0;i<read;++i)
		{
			std::map<std::wstring, std::wstring> extras;
			bool b=getNextEntry(buffer[i], cf, &extras);
			if(b)
			{
				if( !cf.isdir )
				{
					std::string sha256hex=Server->ConvertToUTF8(extras[L"sha256"]);

					if(sha256hex.empty())
					{
						std::string msg="No hash for file \""+Server->ConvertToUTF8(curr_path+os_file_sep()+cf.name)+"\" found. Verification failed.";
						verify_ok=false;
						ServerLogger::Log(clientid, msg, LL_ERROR);
						log << msg << std::endl;
					}
					else if(getSHA256(curr_path+os_file_sep()+cf.name)!=sha256hex)
					{
						std::string msg="Hashes for \""+Server->ConvertToUTF8(curr_path+os_file_sep()+cf.name)+"\" differ. Verification failed.";
						verify_ok=false;
						ServerLogger::Log(clientid, msg, LL_ERROR);
						log << msg << std::endl;
					}
				}
				else
				{
					if(cf.name==L"..")
					{
						curr_path=ExtractFilePath(curr_path);
					}
					else
					{
						curr_path+=os_file_sep()+cf.name;
					}
				}
			}
		}
	}

	if(!verify_ok)
	{
		sendMailToAdmins("File backup verification failed", log.str());
	}

	return verify_ok;
}

std::string BackupServerGet::getSHA256(const std::wstring& fn)
{
	sha256_ctx ctx;
	sha256_init(&ctx);

	IFile * f=Server->openFile(os_file_prefix(fn), MODE_READ);

	if(f==NULL)
	{
		return std::string();
	}

	char buffer[4096];
	unsigned int r;
	while( (r=f->Read(buffer, 4096))>0)
	{
		sha256_update(&ctx, reinterpret_cast<const unsigned char*>(buffer), r);
	}

	Server->destroy(f);

	unsigned char dig[32];
	sha256_final(&ctx, dig);

	return bytesToHex(dig, 32);
}

void BackupServerGet::logVssLogdata(void)
{
	std::string vsslogdata=sendClientMessage("GET VSSLOG", L"Getting volume shadow copy logdata from client failed", 10000, false, LL_INFO);

	if(!vsslogdata.empty() && vsslogdata!="ERR")
	{
		std::vector<std::string> lines;
		TokenizeMail(vsslogdata, lines, "\n");
		for(size_t i=0;i<lines.size();++i)
		{
			int loglevel=atoi(getuntil("-", lines[i]).c_str());
			std::string data=getafter("-", lines[i]);
			ServerLogger::Log(clientid, data, loglevel);
		}
	}
}

bool BackupServerGet::createDirectoryForClient(void)
{
	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	if(!os_create_dir(os_file_prefix(backupfolder+os_file_sep()+clientname)) && !os_directory_exists(os_file_prefix(backupfolder+os_file_sep()+clientname)) )
	{
		Server->Log(L"Could not create or read directory for client \""+clientname+L"\"", LL_ERROR);
		return false;
	}
	return true;
}

void BackupServerGet::createHashThreads(bool use_reflink)
{
	assert(bsh==NULL);
	assert(bsh_prepare==NULL);

	hashpipe=Server->createMemoryPipe();
	hashpipe_prepare=Server->createMemoryPipe();

	bsh=new BackupServerHash(hashpipe, clientid, use_snapshots, use_reflink, use_tmpfiles);
	bsh_prepare=new BackupServerPrepareHash(hashpipe_prepare, hashpipe, clientid);
	bsh_ticket = Server->getThreadPool()->execute(bsh);
	bsh_prepare_ticket = Server->getThreadPool()->execute(bsh_prepare);
}

void BackupServerGet::destroyHashThreads()
{
	hashpipe_prepare->Write("exit");
	Server->getThreadPool()->waitFor(bsh_ticket);
	Server->getThreadPool()->waitFor(bsh_prepare_ticket);

	bsh_ticket=ILLEGAL_THREADPOOL_TICKET;
	bsh_prepare_ticket=ILLEGAL_THREADPOOL_TICKET;
	hashpipe=NULL;
	hashpipe_prepare=NULL;
	bsh=NULL;
	bsh_prepare=NULL;
}

void BackupServerGet::copyFile(const std::wstring& source, const std::wstring& dest)
{
	CWData data;
	data.addInt(BackupServerHash::EAction_Copy);
	data.addString(Server->ConvertToUTF8(source));
	data.addString(Server->ConvertToUTF8(dest));

	hashpipe->Write(data.getDataPtr(), data.getDataSize());
}