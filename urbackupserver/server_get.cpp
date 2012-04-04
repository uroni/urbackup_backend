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

#include "server_get.h"
#include "server_ping.h"
#include "database.h"
#include "../stringtools.h"
#include "fileclient/FileClient.h"
#include "fileclient/FileClientChunked.h"
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/fileclient/data.h"
#include "server_channel.h"
#include "server_log.h"
#include "InternetServiceConnector.h"
#include "server_update_stats.h"
#include "../urbackupcommon/escape.h"
#include "server_running.h"
#include "server_cleanup.h"
#include "treediff/TreeDiff.h"
#include "../urlplugin/IUrlFactory.h"
#include <time.h>
#include <algorithm>
#include <memory.h>

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


int BackupServerGet::running_backups=0;
int BackupServerGet::running_file_backups=0;
IMutex *BackupServerGet::running_backup_mutex=NULL;

BackupServerGet::BackupServerGet(IPipe *pPipe, sockaddr_in pAddr, const std::wstring &pName, bool internet_connection)
	: internet_connection(internet_connection)
{
	q_update_lastseen=NULL;
	pipe=pPipe;
	clientaddr=pAddr;
	clientaddr_mutex=Server->createMutex();
	clientname=pName;
	clientid=0;

	hashpipe=Server->createMemoryPipe();
	hashpipe_prepare=Server->createMemoryPipe();
	exitpipe=Server->createMemoryPipe();
	exitpipe_prepare=Server->createMemoryPipe();

	do_full_backup_now=false;
	do_incr_backup_now=false;
	do_update_settings=false;
	do_full_image_now=false;
	do_incr_image_now=false;
	
	can_backup_images=true;

	filesrv_protocol_version=0;
	file_protocol_version=1;
}

BackupServerGet::~BackupServerGet(void)
{
	if(q_update_lastseen!=NULL)
		unloadSQL();

	Server->destroy(clientaddr_mutex);
}

void BackupServerGet::init_mutex(void)
{
	running_backup_mutex=Server->createMutex();
}

void BackupServerGet::destroy_mutex(void)
{
	Server->destroy(running_backup_mutex);
}

void BackupServerGet::unloadSQL(void)
{
	db->destroyQuery(q_update_lastseen);
	db->destroyQuery(q_update_full);
	db->destroyQuery(q_update_incr);
	db->destroyQuery(q_create_backup);
	db->destroyQuery(q_get_last_incremental);
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
		bool b=sendClientMessage("ADD IDENTITY", "OK", L"Sending Identity to client failed stopping...", 10000, false);
		if(!b)
		{
			pipe->Write("ok");
			Server->Log(L"server_get Thread for client "+clientname+L" finished, because the identity was not recognized", LL_INFO);

			ServerStatus::setWrongIdent(clientname, true);
			ServerLogger::reset(clientid);
			delete this;
			return;
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

	//TEst----------------------------------
	CopyFileA("D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\data\\boost_python-vc100-gd-1_44_old.dll", "D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\boost_python-vc100-gd-1_44.dll", false);
	IFile *tfile=Server->openFile("D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\data\\boost_python-vc100-gd-1_44.dll");
	IFile *old_tfile=Server->openFile("D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\data\\boost_python-vc100-gd-1_44_old.dll");
	IFile *hashfile=Server->openFile("D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\boost_python-vc100-gd-1_44.dll.hash", MODE_RW);
	std::string hash=BackupServerPrepareHash::build_chunk_hashs(old_tfile, hashfile);

	IPipe *cp=InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_FILESRV, 10000);
	CTCPStack stack(internet_connection);
	FileClientChunked fctest(cp, &stack);
	IFile *outputfile=Server->openFile("D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\boost_python-vc100-gd-1_44.dll", MODE_RW);
	IFile *hashoutput=Server->openFile("D:\\Developement\\MSVCProjects\\UrBackupBackend\\urbackup\\boost_python-vc100-gd-1_44.dll.hashoutput", MODE_WRITE);
	_u32 rc=fctest.GetFileChunked("urbackup/boost_python-vc100-gd-1_44.dll", outputfile, hashfile, hashoutput);
	std::wstring fname=outputfile->getFilenameW();
	Server->destroy(outputfile);
	if(rc==ERR_SUCCESS)
	{
		os_file_truncate(fname, fctest.getSize());
	}
	Server->Log("Return code: "+nconvert(rc));
	return;
	//End Test------------------------------


	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	server_settings=new ServerSettings(db);

	clientid=getClientID();

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
	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	if(!os_create_dir(os_file_prefix()+backupfolder+os_file_sep()+clientname) && !os_directory_exists(os_file_prefix()+backupfolder+os_file_sep()+clientname) )
	{
		Server->Log(L"Could not create or read directory for client \""+clientname+L"\"", LL_ERROR);
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

	BackupServerHash *bsh=new BackupServerHash(hashpipe, exitpipe, clientid );
	BackupServerPrepareHash *bsh_prepare=new BackupServerPrepareHash(hashpipe_prepare, exitpipe_prepare, hashpipe, exitpipe, clientid);
	Server->getThreadPool()->execute(bsh);
	Server->getThreadPool()->execute(bsh_prepare);
	ServerChannelThread channel_thread(this, clientid, internet_connection);
	THREADPOOL_TICKET channel_thread_id=Server->getThreadPool()->execute(&channel_thread);

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

	bool skip_checking=false;

	if( server_settings->getSettings()->startup_backup_delay>0 )
	{
		pipe->isReadable(server_settings->getSettings()->startup_backup_delay*1000);
		skip_checking=true;
	}

	bool do_exit_now=false;
	bool tried_backup=false;
	bool file_backup_err=false;
	
	while(true)
	{
		if(!skip_checking)
		{
			if(do_update_settings)
			{
				ServerLogger::Log(clientid, "Getting client settings...", LL_DEBUG);
				do_update_settings=false;
				if(server_settings->getSettings()->allow_overwrite && !getClientSettings())
				{
					ServerLogger::Log(clientid, "Getting client settings failed -2", LL_ERROR);
				}
			}
			tried_backup=false;
			unsigned int ttime=Server->getTimeMS();
			status.starttime=ttime;
			has_error=false;
			bool hbu=false;
			bool r_success=false;
			bool r_image=false;
			r_incremental=false;
			pingthread=NULL;
			pingthread_ticket=ILLEGAL_THREADPOOL_TICKET;
			status.pcdone=0;
			status.hashqueuesize=0;
			status.prepare_hashqueuesize=0;
			ServerStatus::setServerStatus(status);

			bool internet_no_full_file=(internet_connection && !server_settings->getSettings()->internet_full_file_backups );
			bool internet_no_images=(internet_connection && !server_settings->getSettings()->internet_image_backups );

			if(do_incr_image_now)
			{
				if(!can_backup_images)
					Server->Log("Cannot do image backup because can_backup_images=false", LL_DEBUG);
				if(server_settings->getSettings()->no_images)
					Server->Log("Cannot do image backup because no_images=true", LL_DEBUG);
				if(!isBackupsRunningOkay())
					Server->Log("Cannot do image backup because isBackupsRunningOkay()=false", LL_DEBUG);
				if(!internet_no_images )
					Server->Log("Cannot do image backup because internet_no_images=true", LL_DEBUG);
			}

			if( !file_backup_err && !server_settings->getSettings()->no_file_backups && !internet_no_full_file && isBackupsRunningOkay() && ( (isUpdateFull() && isInBackupWindow(server_settings->getBackupWindow())) || do_full_backup_now ) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_full_file;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing full file backup...", LL_DEBUG);
				do_full_backup_now=false;

				hbu=true;
				startBackupRunning(true);
				if(!constructBackupPath())
				{
					ServerLogger::Log(clientid, "Cannot create Directory for backup (Server error)", LL_ERROR);
					r_success=false;
				}
				else
				{
					pingthread=new ServerPingThread(this);
					pingthread_ticket=Server->getThreadPool()->execute(pingthread);

					r_success=doFullBackup();
				}
			}
			else if( !file_backup_err && !server_settings->getSettings()->no_file_backups && isBackupsRunningOkay() && ( (isUpdateIncr() && isInBackupWindow(server_settings->getBackupWindow())) || do_incr_backup_now ) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_incr_file;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing incremental file backup...", LL_DEBUG);
				do_incr_backup_now=false;
				hbu=true;
				startBackupRunning(true);
				r_incremental=true;
				if(!constructBackupPath())
				{
					ServerLogger::Log(clientid, "Cannot create Directory for backup (Server error)", LL_ERROR);
					r_success=false;
				}
				else
				{
					pingthread=new ServerPingThread(this);
					pingthread_ticket=Server->getThreadPool()->execute(pingthread);

					r_success=doIncrBackup();
				}
			}
			else if(can_backup_images && !server_settings->getSettings()->no_images && !internet_no_images && isBackupsRunningOkay() && ( (isUpdateFullImage() && isInBackupWindow(server_settings->getBackupWindow())) || do_full_image_now) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_full_image;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing full image backup...", LL_DEBUG);
				
				r_image=true;

				pingthread=new ServerPingThread(this);
				pingthread_ticket=Server->getThreadPool()->execute(pingthread);

				startBackupRunning(false);

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
				
							if(doImage("SYSVOL", L"", 0, 0))
							{
								sysvol_id=backupid;
							}
							ServerLogger::Log(clientid, "Backing up SYSVOL done.", LL_DEBUG);
						}
						bool b=doImage(vols[i]+":", L"", 0, 0);
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
			else if(can_backup_images && !server_settings->getSettings()->no_images && !internet_no_images && isBackupsRunningOkay() && ( (isUpdateIncrImage() && isInBackupWindow(server_settings->getBackupWindow())) || do_incr_image_now) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_incr_image;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing incremental image backup...", LL_DEBUG);
				
				r_image=true;
				r_incremental=true;

				startBackupRunning(false);
			
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
							if(doImage("SYSVOL", L"", 0, 0))
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
							r_success=doImage(letter, last.path, last.incremental+1, last.incremental_ref);
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

			if(hbu && !has_error)
			{
				if(r_success)
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
			else if(hbu && has_error)
			{
				file_backup_err=true;
				os_remove_nonempty_dir(backuppath);
				tried_backup=true;
			}

			status.action_done=false;
			status.statusaction=sa_none;
			status.pcdone=100;
			//Flush buffer before continuing...
			status.hashqueuesize=(_u32)hashpipe->getNumElements()+(bsh->isWorking()?1:0);
			status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements()+(bsh_prepare->isWorking()?1:0);
			hashpipe->Write("flush");
			while(status.hashqueuesize>0 || status.prepare_hashqueuesize>0)
			{
				ServerStatus::setServerStatus(status, true);
				Server->wait(1000);
				status.hashqueuesize=(_u32)hashpipe->getNumElements()+(bsh->isWorking()?1:0);
				status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements()+(bsh_prepare->isWorking()?1:0);
			}
			ServerStatus::setServerStatus(status);

			unsigned int ptime=Server->getTimeMS()-ttime;
			if(hbu && !has_error)
			{
				ServerLogger::Log(clientid, L"Time taken for backing up client "+clientname+L": "+convert(ptime), LL_INFO);
				if(!r_success)
				{
					ServerLogger::Log(clientid, "Backup not complete because of connection problems", LL_ERROR);
				}
				else if( bsh->hasError() )
				{
					ServerLogger::Log(clientid, "Backup not complete because of disk problems", LL_ERROR);
				}
				else
				{
					updateLastBackup();
					setBackupComplete();
				}
				status.pcdone=100;
				ServerStatus::setServerStatus(status, true);
			}

			if( r_image )
			{
				ServerLogger::Log(clientid, L"Time taken for creating image of client "+clientname+L": "+convert(ptime), LL_INFO);
				if(!r_success)
				{
					ServerLogger::Log(clientid, "Backup not complete because of connection problems", LL_ERROR);
				}
				else
				{
					updateLastImageBackup();
				}
				status.pcdone=100;
				ServerStatus::setServerStatus(status, true);
			}

			if(hbu || r_image)
			{
				stopBackupRunning(!r_image);
				saveClientLogdata(r_image?1:0, r_incremental?1:0, r_success && !has_error);
				sendClientLogdata();
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
		if(file_backup_err)
			pipe->Read(&msg, 0);
		else if(tried_backup)
			pipe->Read(&msg, skip_checking?0:check_time_intervall_tried_backup);
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

		Server->Log("msg="+msg, LL_DEBUG);
	}

	//destroy channel
	{
		Server->Log("Stopping channel...", LL_DEBUG);
		channel_thread.doExit();
		Server->getThreadPool()->waitFor(channel_thread_id);
	}

	if(do_exit_now)
	{
		hashpipe_prepare->Write("exitnow");
		std::string msg;
		exitpipe_prepare->Read(&msg);
		Server->destroy(exitpipe_prepare);
	}
	else
	{
		hashpipe_prepare->Write("exit");
	}
	
	
	Server->destroy(settings);
	Server->destroy(settings_client);
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
	q_create_backup=db->Prepare("INSERT INTO backups (incremental, clientid, path, complete, running, size_bytes, done) VALUES (?, ?, ?, 0, CURRENT_TIMESTAMP, -1, 0)", false);
	q_get_last_incremental=db->Prepare("SELECT incremental,path FROM backups WHERE clientid=? AND done=1 ORDER BY backuptime DESC LIMIT 1", false);
	q_set_last_backup=db->Prepare("UPDATE clients SET lastbackup=CURRENT_TIMESTAMP WHERE id=?", false);
	q_update_setting=db->Prepare("UPDATE settings_db.settings SET value=? WHERE key=? AND clientid=?", false);
	q_insert_setting=db->Prepare("INSERT INTO settings_db.settings (key, value, clientid) VALUES (?,?,?)", false);
	q_set_complete=db->Prepare("UPDATE backups SET complete=1 WHERE id=?", false);
	q_update_image_full=db->Prepare("SELECT id FROM backup_images WHERE datetime('now','-"+nconvert(s->update_freq_image_full)+" seconds')<backuptime AND clientid=? AND incremental=0 AND complete=1 AND version="+nconvert(curr_image_version)+" AND letter=?", false);
	q_update_image_incr=db->Prepare("SELECT id FROM backup_images WHERE datetime('now','-"+nconvert(s->update_freq_image_incr)+" seconds')<backuptime AND clientid=? AND complete=1 AND version="+nconvert(curr_image_version)+" AND letter=?", false); 
	q_create_backup_image=db->Prepare("INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes, version, letter) VALUES (?, ?, ?, ?, 0, CURRENT_TIMESTAMP, 0, "+nconvert(curr_image_version)+",?)", false);
	q_set_image_size=db->Prepare("UPDATE backup_images SET size_bytes=? WHERE id=?", false);
	q_set_image_complete=db->Prepare("UPDATE backup_images SET complete=1 WHERE id=?", false);
	q_set_last_image_backup=db->Prepare("UPDATE clients SET lastbackup_image=CURRENT_TIMESTAMP WHERE id=?", false);
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

int BackupServerGet::getClientID(void)
{
	IQuery *q=db->Prepare("SELECT id FROM clients WHERE name=?",false);
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

		if(c_clients<server_settings->getSettings()->max_active_clients)
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
		return b;
	}
	else
	{
		SBackup b;
		b.incremental=-2;
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
	q_update_full->Bind(clientid);
	db_results res=q_update_full->Read();
	q_update_full->Reset();
	return res.empty();
}

bool BackupServerGet::isUpdateIncr(void)
{
	q_update_incr->Bind(clientid);
	db_results res=q_update_incr->Read();
	q_update_incr->Reset();
	return res.empty();
}

bool BackupServerGet::isUpdateFullImage(const std::string &letter)
{
	if(server_settings->getSettings()->update_freq_image_full<0)
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
	if(server_settings->getSettings()->update_freq_image_full<=0)
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

bool BackupServerGet::getNextEntry(char ch, SFile &data)
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
		if(ch!='\n')
		{
			t_name+=ch;
		}
		else
		{
			data.last_modified=os_atoi64(t_name);
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

bool BackupServerGet::request_filelist_construct(bool full, bool with_token)
{
	unsigned int timeout_time=full_backup_construct_timeout;
	if(file_protocol_version>=2)
	{
		timeout_time=120000;
	}

	CTCPStack tcpstack(internet_connection);

	Server->Log(clientname+L": Connecting for filelist...", LL_DEBUG);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed - CONNECT error during filelist construction", LL_ERROR);
		return false;
	}

	std::string pver="";
	if(file_protocol_version==2) pver="2";

	if(full)
		tcpstack.Send(cc, server_identity+pver+"START FULL BACKUP"+(with_token?("#token="+server_token):""));
	else
		tcpstack.Send(cc, server_identity+pver+"START BACKUP"+(with_token?("#token="+server_token):""));

	Server->Log(clientname+L": Waiting for filelist", LL_DEBUG);
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
				Server->Log(clientname+L": Trying old filelist request", LL_WARNING);
				return request_filelist_construct(full, false);
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
					ServerLogger::Log(clientid, L"Constructing of filelist of \""+clientname+L"\" failed: "+widen(ret), LL_ERROR);
					break;
				}
				else
				{
					ServerLogger::Log(clientid, L"Constructing of filelist of \""+clientname+L"\" failed: "+widen(ret), LL_DEBUG);
					break;
				}				
			}
			else
			{
				Server->destroy(cc);
				return true;
			}
		}
	}
	Server->destroy(cc);
	return false;
}

bool BackupServerGet::doFullBackup(void)
{
	int64 free_space=os_free_space(os_file_prefix()+server_settings->getSettings()->backupfolder);
	if(free_space!=-1 && free_space<minfreespace_min)
	{
		Server->Log("No space for directory entries. Freeing space", LL_WARNING);
		ServerCleanupThread cleanup;
		if(!cleanup.do_cleanup(minfreespace_min) )
		{
			ServerLogger::Log(clientid, "Could not free space for directory entries. NOT ENOUGH FREE SPACE.", LL_ERROR);
			return false;
		}
	}
	
	
	bool b=request_filelist_construct(true);
	if(!b)
	{
		has_error=true;
		return false;
	}

	FileClient fc(filesrv_protocol_version);
	_u32 rc=getClientFilesrvConnection(&fc, 10000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, L"Full Backup of "+clientname+L" failed - CONNECT error", LL_ERROR);
		has_error=true;
		return false;
	}
	
	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return false;

	rc=fc.GetFile("urbackup/filelist.ub", tmp);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting filelist of "+clientname+L". Errorcode: "+convert(rc), LL_ERROR);
		has_error=true;
		return false;
	}

	backupid=createBackupSQL(0, clientid, backuppath_single);
	
	tmp->Seek(0);
	
	resetEntryState();

	IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+".ub", MODE_WRITE);

	if(clientlist==NULL )
	{
		ServerLogger::Log(clientid, L"Error creating clientlist for client "+clientname, LL_ERROR);
		has_error=true;
		return false;
	}
	_i64 filelist_size=tmp->Size();

	char buffer[4096];
	_u32 read;
	_i64 filelist_currpos=0;
	std::wstring curr_path;
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

			bool b=getNextEntry(buffer[i], cf);
			if(b)
			{
				if(cf.isdir==true)
				{
					if(cf.name!=L"..")
					{
						curr_path+=L"/"+cf.name;
						std::wstring os_curr_path=curr_path;
						if(os_file_sep()!=L"/")
						{
							for(size_t i=0;i<os_curr_path.size();++i)
								if(os_curr_path[i]=='/')
									os_curr_path[i]=os_file_sep()[0];
						}
						if(!os_create_dir(os_file_prefix()+backuppath+os_curr_path))
						{
							ServerLogger::Log(clientid, L"Creating directory  \""+backuppath+os_curr_path+L"\" failed.", LL_ERROR);
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
					}
				}
				else
				{
					bool b=load_file(cf.name, curr_path, fc);
					if(!b)
					{
						ServerLogger::Log(clientid, L"Client "+clientname+L" went offline.", LL_ERROR);
						r_done=true;
						break;
					}
					transferred+=cf.size;
				}
			}
		}
		writeFileRepeat(clientlist, buffer, read);
		if(read<4096)
			break;
	}
	if(r_done==false && c_has_error==false)
	{
		std::wstring backupfolder=server_settings->getSettings()->backupfolder;
		std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+L"current";
		Server->deleteFile(os_file_prefix()+currdir);
		os_link_symbolic(os_file_prefix()+backuppath, os_file_prefix()+currdir);

		currdir=backupfolder+os_file_sep()+L"clients";
		if(!os_create_dir(os_file_prefix()+currdir) && !os_directory_exists(os_file_prefix()+currdir))
		{
			Server->Log("Error creating \"clients\" dir for symbolic links", LL_ERROR);
		}
		currdir+=os_file_sep()+clientname;
		Server->deleteFile(os_file_prefix()+currdir);
		os_link_symbolic(os_file_prefix()+backuppath, os_file_prefix()+currdir);
	}
	running_updater->stop();
	updateRunning(false);
	Server->destroy(clientlist);
	Server->destroy(tmp);

	setBackupDone();
	
	if(c_has_error)
		return false;

	return !r_done;
}

bool BackupServerGet::load_file(const std::wstring &fn, const std::wstring &curr_path, FileClient &fc)
{
	ServerLogger::Log(clientid, L"Loading file \""+fn+L"\"", LL_DEBUG);
	IFile *fd=NULL;
	while(fd==NULL)
	{
		fd=Server->openTemporaryFile();
		if(fd==NULL)
		{
			ServerLogger::Log(clientid, "Error opening temporary file. Retrying...", LL_WARNING);
			Server->wait(500);
		}
	}

	std::wstring cfn=curr_path+L"/"+fn;
	if(cfn[0]=='/')
		cfn.erase(0,1);

	if(!server_token.empty())
	{
		cfn=widen(server_token)+L"|"+cfn;
	}
	
	_u32 rc=fc.GetFile(Server->ConvertToUTF8(cfn), fd);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting file \""+cfn+L"\" from "+clientname+L". Errorcode: "+convert(rc), LL_ERROR);
		std::wstring temp_fn=fd->getFilenameW();
		Server->destroy(fd);
		Server->deleteFile(temp_fn);
		if(rc==ERR_TIMEOUT || rc==ERR_ERROR || rc==ERR_BASE_DIR_LOST)
			return false;
	}
	else
	{
		std::wstring os_curr_path=curr_path+L"/"+fn;
		if(os_file_sep()!=L"/")
		{
			for(size_t i=0;i<os_curr_path.size();++i)
				if(os_curr_path[i]=='/')
					os_curr_path[i]=os_file_sep()[0];
		}
		
		std::wstring dstpath=backuppath+os_curr_path;

		hashFile(dstpath, fd);
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
			bool b=getNextEntry(buffer[i], cf);
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

bool BackupServerGet::doIncrBackup(void)
{
	int64 free_space=os_free_space(os_file_prefix()+server_settings->getSettings()->backupfolder);
	if(free_space!=-1 && free_space<minfreespace_min)
	{
		Server->Log("No space for directory entries. Freeing space", LL_WARNING);
		ServerCleanupThread cleanup;
		if(!cleanup.do_cleanup(minfreespace_min) )
		{
			ServerLogger::Log(clientid, "Could not free space for directory entries. NOT ENOUGH FREE SPACE.", LL_ERROR);
			return false;
		}
	}
	
	bool b=request_filelist_construct(false);
	if(!b)
	{
		has_error=true;
		return false;
	}

	Server->Log(clientname+L": Connecting to client...", LL_DEBUG);
	FileClient fc(filesrv_protocol_version);
	_u32 rc=getClientFilesrvConnection(&fc, 10000);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, L"Incremental Backup of "+clientname+L" failed - CONNECT error", LL_ERROR);
		has_error=true;
		return false;
	}
	
	Server->Log(clientname+L": Loading filelist...", LL_DEBUG);
	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL) return false;

	rc=fc.GetFile("urbackup/filelist.ub", tmp);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting filelist of "+clientname+L". Errorcode: "+convert(rc), LL_ERROR);
		has_error=true;
		return false;
	}
	
	Server->Log(clientname+L" Starting incremental backup...", LL_DEBUG);

	SBackup last=getLastIncremental();
	if(last.incremental==-2)
	{
		ServerLogger::Log(clientid, "Error retrieving last backup.", LL_ERROR);
		has_error=true;
		return false;
	}
	backupid=createBackupSQL(last.incremental+1, clientid, backuppath_single);

	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	std::wstring last_backuppath=backupfolder+os_file_sep()+clientname+os_file_sep()+last.path;

	std::wstring tmpfilename=tmp->getFilenameW();
	Server->destroy(tmp);

	Server->Log(clientname+L": Calculating file tree differences...", LL_DEBUG);
	bool error=false;
	std::vector<size_t> diffs=TreeDiff::diffTrees("urbackup/clientlist_"+nconvert(clientid)+".ub", wnarrow(tmpfilename), error);
	if(error)
	{
		ServerLogger::Log(clientid, "Error while calculating tree diff. Doing full backup.", LL_ERROR);
		return doFullBackup();
	}

	IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+"_new.ub", MODE_WRITE);

	tmp=Server->openFile(tmpfilename, MODE_READ);

	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, false);
	Server->getThreadPool()->execute(running_updater);
	
	resetEntryState();
	
	char buffer[4096];
	_u32 read;
	std::wstring curr_path;
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
	
	Server->Log(clientname+L": Calculating tree difference size...", LL_DEBUG);
	_i64 files_size=getIncrementalSize(tmp, diffs);
	tmp->Seek(0);
	_i64 transferred=0;
	
	unsigned int laststatsupdate=0;
	ServerStatus::setServerStatus(status, true);
	
	Server->Log(clientname+L": Linking unchanged and loading new files...", LL_DEBUG);
	
	bool c_has_error=false;

	while( (read=tmp->Read(buffer, 4096))>0 )
	{
		filelist_currpos+=read;

		for(size_t i=0;i<read;++i)
		{
			bool b=getNextEntry(buffer[i], cf);
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
						std::wstring os_curr_path=curr_path;
						if(os_file_sep()!=L"/")
						{
							for(size_t i=0;i<os_curr_path.size();++i)
								if(os_curr_path[i]=='/')
									os_curr_path[i]=os_file_sep()[0];
						}
						if(!os_create_dir(os_file_prefix()+backuppath+os_curr_path))
						{
							ServerLogger::Log(clientid, L"Creating directory  \""+backuppath+os_curr_path+L"\" failed.", LL_ERROR);
							c_has_error=true;
							break;
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
					}
				}
				else
				{
					if(indirchange==true || hasChange(line, diffs))
					{
						if(r_offline==false)
						{
							bool b=load_file(cf.name, curr_path, fc);
							if(!b)
							{
								ServerLogger::Log(clientid, L"Client "+clientname+L" went offline.", LL_ERROR);
								r_offline=true;
							}
							else
							{
								transferred+=cf.size;
								writeFileRepeat(clientlist, "f\""+Server->ConvertToUTF8(cf.name)+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
							}
						}
					}
					else
					{			
						std::wstring os_curr_path=curr_path+L"/"+cf.name;
						if(os_file_sep()!=L"/")
						{
							for(size_t i=0;i<os_curr_path.size();++i)
								if(os_curr_path[i]=='/')
									os_curr_path[i]=os_file_sep()[0];
						}

						std::wstring srcpath=last_backuppath+os_curr_path;

						bool f_ok=true;

						bool b=os_create_hardlink(os_file_prefix()+backuppath+os_curr_path, os_file_prefix()+srcpath);
						if(!b)
						{
							if(link_logcnt<5)
							{
								ServerLogger::Log(clientid, L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+os_curr_path+L"\" failed. Loading file...", LL_WARNING);
							}
							else if(link_logcnt==5)
							{
								ServerLogger::Log(clientid, L"More warnings of kind: Creating hardlink from \""+srcpath+L"\" to \""+backuppath+os_curr_path+L"\" failed. Loading file... Skipping.", LL_WARNING);
							}
							else
							{
								Server->Log(L"Creating hardlink from \""+srcpath+L"\" to \""+backuppath+os_curr_path+L"\" failed. Loading file...", LL_WARNING);
							}
							++link_logcnt;
							if(r_offline==false)
							{
								bool b2=load_file(cf.name, curr_path, fc);
								if(!b2)
								{
									ServerLogger::Log(clientid, L"Client "+clientname+L" went offline.", LL_ERROR);
									r_offline=true;
									f_ok=false;
								}
							}
							else
							{
								f_ok=false;
							}
						}

						if(f_ok)
						{
							writeFileRepeat(clientlist, "f\""+Server->ConvertToUTF8(cf.name)+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
						}
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
	if(r_offline==false && c_has_error==false)
	{
		Server->Log("Client ok. Copying full file...", LL_DEBUG);
		IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+".ub", MODE_WRITE);
		bool clientlist_copy_err=false;
		if(clientlist!=NULL)
		{
			tmp->Seek(0);
			_u32 r=0;
			char buf[4096];
			do
			{
				r=tmp->Read(buf, 4096);
				if(r>0)
				{
					_u32 written=0;
					_u32 rc;
					int tries=50;
					do
					{
						rc=clientlist->Write(buf+written, r-written);
						written+=rc;
						if(rc==0)
						{
							Server->Log("Failed to write to file... waiting...", LL_WARNING);
							Server->wait(10000);
							--tries;
						}
					}
					while(written<r && (rc>0 || tries>0) );
					if(rc==0)
					{
						ServerLogger::Log(clientid, "Fatal error copying clientlist. Write error.", LL_ERROR);
						clientlist_copy_err=true;
						break;
					}
				}
			}
			while(r>0);
			Server->Log("Copying done.", LL_DEBUG);
		
			Server->Log("Creating symbolic links. -1", LL_DEBUG);

			std::wstring backupfolder=server_settings->getSettings()->backupfolder;
			std::wstring currdir=backupfolder+os_file_sep()+clientname+os_file_sep()+L"current";
			Server->deleteFile(os_file_prefix()+currdir);
			os_link_symbolic(os_file_prefix()+backuppath, os_file_prefix()+currdir);

			Server->Log("Creating symbolic links. -2", LL_DEBUG);

			currdir=backupfolder+os_file_sep()+L"clients";
			if(!os_create_dir(os_file_prefix()+currdir) && !os_directory_exists(os_file_prefix()+currdir))
			{
				Server->Log("Error creating \"clients\" dir for symbolic links", LL_ERROR);
			}
			currdir+=os_file_sep()+clientname;
			Server->deleteFile(os_file_prefix()+currdir);
			os_link_symbolic(os_file_prefix()+backuppath, os_file_prefix()+currdir);

			Server->Log("Symbolic links created.", LL_DEBUG);

			Server->destroy(clientlist);
		}
		else
		{
			ServerLogger::Log(clientid, "Fatal error copying clientlist. Open error.", LL_ERROR);
		}
		if(!clientlist_copy_err)
		{
			Server->deleteFile("urbackup/clientlist_"+nconvert(clientid)+"_new.ub");
		}
	}
	else if(!c_has_error)
	{
		Server->Log("Client disconnected while backing up. Copying partial file...", LL_DEBUG);
		Server->deleteFile("urbackup/clientlist_"+nconvert(clientid)+".ub");
		moveFile(L"urbackup/clientlist_"+convert(clientid)+L"_new.ub", L"urbackup/clientlist_"+convert(clientid)+L".ub");
	}
	else
	{
		ServerLogger::Log(clientid, "Fatal error during backup. Backup not completed", LL_ERROR);
	}

	running_updater->stop();
	updateRunning(false);
	Server->destroy(tmp);
	Server->deleteFile(tmpfilename);

	setBackupDone();

	if(c_has_error) return false;
	
	return !r_offline;
}

bool BackupServerGet::hasChange(size_t line, const std::vector<size_t> &diffs)
{
	return std::binary_search(diffs.begin(), diffs.end(), line);
}

void BackupServerGet::hashFile(std::wstring dstpath, IFile *fd)
{
	int l_backup_id=backupid;

	CWData data;
	data.addString(Server->ConvertToUTF8(fd->getFilenameW()));
	data.addInt(l_backup_id);
	data.addChar(r_incremental==true?1:0);
	data.addString(Server->ConvertToUTF8(dstpath));

	ServerLogger::Log(clientid, "GT: Loaded file \""+ExtractFileName(Server->ConvertToUTF8(dstpath))+"\"", LL_DEBUG);

	Server->destroy(fd);
	hashpipe_prepare->Write(data.getDataPtr(), data.getDataSize() );
}

bool BackupServerGet::constructBackupPath(void)
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
	return os_create_dir(os_file_prefix()+backuppath);	
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
	q_set_last_backup->Bind(clientid);
	q_set_last_backup->Write();
	q_set_last_backup->Reset();
}

void BackupServerGet::updateLastImageBackup(void)
{
	q_set_last_image_backup->Bind(clientid);
	q_set_last_image_backup->Write();
	q_set_last_image_backup->Reset();
}

std::string BackupServerGet::sendClientMessage(const std::string &msg, const std::wstring &errmsg, unsigned int timeout, bool logerr)
{
	CTCPStack tcpstack(internet_connection);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		if(logerr)
			ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, LL_ERROR);
		else
			Server->Log(L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, LL_DEBUG);
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
			ServerLogger::Log(clientid, errmsg, LL_ERROR);
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

	ServerLogger::Log(clientid, L"Timeout: "+errmsg, LL_ERROR);

	Server->destroy(cc);

	return "";
}

bool BackupServerGet::sendClientMessage(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, bool logerr)
{
	CTCPStack tcpstack(internet_connection);
	IPipe *cc=getClientCommandConnection(10000);
	if(cc==NULL)
	{
		if(logerr)
			ServerLogger::Log(clientid, L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, LL_ERROR);
		else
			Server->Log(L"Connecting to ClientService of \""+clientname+L"\" failed: "+errmsg, LL_DEBUG);
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
			if(logerr)
				ServerLogger::Log(clientid, errmsg, LL_ERROR);
			else
				Server->Log(errmsg, LL_ERROR);
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
					ServerLogger::Log(clientid, errmsg, LL_ERROR);
				else
					Server->Log(errmsg, LL_ERROR);
				break;
			}
			else
			{
				ok=true;
				break;
			}
		}
	}
	if(!ok && !herr)
	{
		if(logerr)
			ServerLogger::Log(clientid, L"Timeout: "+errmsg, LL_ERROR);
		else
			Server->Log(L"Timeout: "+errmsg, LL_ERROR);
	}

	Server->destroy(cc);

	return ok;
}

void BackupServerGet::start_shadowcopy(const std::string &path)
{
	sendClientMessage("START SC \""+path+"\"#token="+server_token, "DONE", L"Activating shadow copy on \""+clientname+L"\" failed", shadow_copy_timeout);
}

void BackupServerGet::stop_shadowcopy(const std::string &path)
{
	sendClientMessage("STOP SC \""+path+"\"#token="+server_token, "DONE", L"Removing shadow copy on \""+clientname+L"\" failed", shadow_copy_timeout);
}

void BackupServerGet::notifyClientBackupSuccessfull(void)
{
	sendClientMessage("DID BACKUP", "OK", L"Sending status to client failed", 10000);
}

void BackupServerGet::sendClientBackupIncrIntervall(void)
{
	sendClientMessage("INCRINTERVALL \""+nconvert(server_settings->getSettings()->update_freq_incr)+"\"", "OK", L"Sending incrintervall to client failed", 10000);
}

bool BackupServerGet::updateCapabilities(void)
{
	std::string cap=sendClientMessage("CAPA", L"Querying capabilities failed", 10000, false);
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
	}

	return !cap.empty();
}

void BackupServerGet::sendSettings(void)
{
	std::string s_settings;

	std::vector<std::wstring> settings_names=getSettingsList();

	for(size_t i=0;i<settings_names.size();++i)
	{
		std::wstring key=settings_names[i];
		std::wstring value;
		if(!settings_client->getValue(key, &value) )
		{
			if(!settings->getValue(key, &value) )
				key=L"";
			else
				key+=L"_def";
		}

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
	FileClient fc(filesrv_protocol_version);
	_u32 rc=getClientFilesrvConnection(&fc);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, L"Getting Client settings of "+clientname+L" failed - CONNECT error", LL_ERROR);
		return false;
	}
	
	IFile *tmp=Server->openTemporaryFile();
	if(tmp==NULL)
	{
		ServerLogger::Log(clientid, "Error creating temporary file in BackupServerGet::getClientSettings", LL_ERROR);
		return false;
	}
	rc=fc.GetFile("urbackup/settings.cfg", tmp);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting Client settings of "+clientname+L". Errorcode: "+convert(rc), LL_ERROR);
		return false;
	}

	ISettingsReader *sr=Server->createFileSettingsReader(tmp->getFilename());

	std::vector<std::wstring> setting_names=getSettingsList();

	bool mod=false;
	for(size_t i=0;i<setting_names.size();++i)
	{
		std::wstring &key=setting_names[i];
		std::wstring value;
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
		sendClientMessage("2LOGDATA "+wnarrow(res[i][L"created"])+" "+logdata, "OK", L"Sending logdata to client failed", 10000, false);
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
				if(toks[j]==res_users[i][L"id"])
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
		ServerLogger::Log(clientid, L"Could not read MBR", LL_ERROR);
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

			std::string msg="CLIENTUPDATE "+nconvert(datasize);
			tcpstack.Send(cc, server_identity+msg);

			int timeout=10000;

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
			while(Server->getTimeMS()-starttime<=10000)
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

			ServerLogger::Log(clientid, L"Updated client successfully", LL_INFO);
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

std::string BackupServerGet::strftimeInt(std::string fs)
{
	time_t rawtime;		
	char buffer [100];
	time ( &rawtime );
#ifdef _WIN32
	struct tm  timeinfo;
	localtime_s(&timeinfo, &rawtime);
	strftime (buffer,100,fs.c_str(),&timeinfo);
#else
	struct tm *timeinfo;
	timeinfo = localtime ( &rawtime );
	strftime (buffer,100,fs.c_str(),timeinfo);
#endif	
	std::string r(buffer);
	return r;
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
	int dow=atoi(strftimeInt("%w").c_str());
	if(dow==0) dow=7;
	
	float hm=(float)atoi(remLeadingZeros(strftimeInt("%H")).c_str())+(float)atoi(remLeadingZeros(strftimeInt("%M")).c_str())*(1.f/60.f);
	for(size_t i=0;i<bw.size();++i)
	{
		if(bw[i].dayofweek==dow)
		{
			if(hm>=bw[i].start_hour && hm<=bw[i].stop_hour )
			{
				return true;
			}
		}
	}

	return false;
}

bool BackupServerGet::isBackupsRunningOkay(void)
{
	IScopedLock lock(running_backup_mutex);
	if(running_backups<server_settings->getSettings()->max_sim_backups)
	{
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
	_u32 rc;
	int tries=50;
	do
	{
		rc=f->Write(buf+written, (_u32)(bsize-written));
		written+=rc;
		if(rc==0)
		{
			Server->Log("Failed to write to file... waiting...", LL_WARNING);
			Server->wait(10000);
			--tries;
		}
	}
	while(written<bsize && (rc>0 || tries>0) );

	if(rc==0)
	{
		Server->Log("Fatal error writing to file in writeFileRepeat. Write error.", LL_ERROR);
	}
}

IPipe *BackupServerGet::getClientCommandConnection(int timeoutms)
{
	if(internet_connection)
	{
		return InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_COMMANDS, timeoutms);
	}
	else
	{
		return Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, timeoutms);
	}
}

_u32 BackupServerGet::getClientFilesrvConnection(FileClient *fc, int timeoutms)
{
	if(internet_connection)
	{
		IPipe *cp=InternetServiceConnector::getConnection(Server->ConvertToUTF8(clientname), SERVICE_FILESRV, timeoutms);
		return fc->Connect(cp);
	}
	else
	{
		sockaddr_in addr=getClientaddr();
		return fc->Connect(&addr);
	}
}

#endif //CLIENT_ONLY