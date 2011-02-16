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
#include "../Interface/Server.h"
#include "../Interface/ThreadPool.h"
#include "fileclient/tcpstack.h"
#include "fileclient/data.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IVHDFile.h"
#include "server_channel.h"
#include "server_log.h"
#include "server_writer.h"
#include "server_cleanup.h"
#include "escape.h"
#include "mbr_code.h"
#include "zero_hash.h"
#include "server_running.h"
#include "treediff/TreeDiff.h"
#include <time.h>
#include <algorithm>
#include <memory.h>

extern IFSImageFactory *image_fak;
extern std::string server_identity;

const unsigned short serviceport=35623;
const unsigned int full_backup_construct_timeout=60*60*1000;
const unsigned int shadow_copy_timeout=5*60*1000;
const unsigned int check_time_intervall=5*60*1000;
const unsigned int status_update_intervall=1000;
const unsigned int mbr_size=(1024*1024)/4;
const size_t minfreespace_image=1000*1024*1024; //1000 MB

BackupServerGet::BackupServerGet(IPipe *pPipe, sockaddr_in pAddr, const std::string &pName)
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
}

BackupServerGet::~BackupServerGet(void)
{
	if(q_update_lastseen!=NULL)
		unloadSQL();

	Server->destroy(clientaddr_mutex);
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
}

void BackupServerGet::operator ()(void)
{
	{
		bool b=sendClientMessage("ADD IDENTITY", "OK", L"Sending Identity to client failed stopping...", 10000, false);
		if(!b)
		{
			pipe->Write("ok");
			Server->Log("server_get Thread for client "+clientname+" finished, because the identity was not recognized");

			ServerLogger::reset(clientid);
			delete this;
			return;
		}
	}

	if( clientname.find("##restore##")==0 )
	{
		ServerChannelThread channel_thread(this, getClientaddr());
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
		Server->Log("server_get Thread for client "+clientname+" finished, restore thread");
		delete this;
		return;
	}


	db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_SERVER);

	clientid=getClientID();

	settings=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings WHERE key=? AND clientid=0");
	settings_client=Server->createDBSettingsReader(db, "settings", "SELECT value FROM settings WHERE key=? AND clientid="+nconvert(clientid));

	server_settings=new ServerSettings(db, clientid);
	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	if(!os_create_dir(backupfolder+os_file_sep()+widen(clientname)) && !os_directory_exists(backupfolder+os_file_sep()+widen(clientname)) )
	{
		Server->Log("Could not create or read directory for client \""+clientname+"\"", LL_ERROR);
		pipe->Write("ok");
		delete server_settings;
		delete this;
		return;
	}

	prepareSQL();

	updateLastseen();	

	status.client=clientname;
	status.clientid=clientid;
	ServerStatus::setServerStatus(status);

	BackupServerHash *bsh=new BackupServerHash(hashpipe_prepare, exitpipe, clientid );
	BackupServerPrepareHash *bsh_prepare=new BackupServerPrepareHash(hashpipe, exitpipe_prepare, hashpipe_prepare, exitpipe, clientid);
	Server->getThreadPool()->execute(bsh);
	Server->getThreadPool()->execute(bsh_prepare);
	ServerChannelThread channel_thread(this, getClientaddr());
	THREADPOOL_TICKET channel_thread_id=Server->getThreadPool()->execute(&channel_thread);

	sendSettings();

	ServerLogger::Log(clientid, "Getting client settings...", LL_DEBUG);
	if(server_settings->getSettings()->allow_overwrite && !getClientSettings())
	{
		ServerLogger::Log(clientid, "Getting client settings failed -1", LL_ERROR);
	}

	ServerLogger::Log(clientid, "Sending backup incr intervall...", LL_DEBUG);
	sendClientBackupIncrIntervall();

	checkClientVersion();

	sendClientLogdata();

	bool skip_checking=false;

	if( server_settings->getSettings()->startup_backup_delay>0 )
	{
		pipe->isReadable(server_settings->getSettings()->startup_backup_delay*1000);
		skip_checking=true;
	}

	bool do_exit_now=false;
	
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
			unsigned int ttime=Server->getTimeMS();
			status.starttime=ttime;
			has_error=false;
			bool hbu=false;
			bool r_success=false;
			bool r_image=false;
			bool r_incremental=false;
			pingthread=NULL;
			pingthread_ticket=ILLEGAL_THREADPOOL_TICKET;
			status.pcdone=0;
			status.hashqueuesize=0;
			status.prepare_hashqueuesize=0;
			ServerStatus::setServerStatus(status);
			if(isUpdateFull() || do_full_backup_now)
			{
				ScopedActiveThread sat;

				status.statusaction=sa_full_file;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing full file backup...", LL_DEBUG);
				do_full_backup_now=false;

				hbu=true;
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
			else if(isUpdateIncr() || do_incr_backup_now)
			{
				ScopedActiveThread sat;

				status.statusaction=sa_incr_file;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing incremental file backup...", LL_DEBUG);
				do_incr_backup_now=false;
				hbu=true;
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
			else if(!server_settings->getSettings()->no_images && (isUpdateFullImage() || do_full_image_now) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_full_image;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing full image backup...", LL_DEBUG);
				do_full_image_now=false;
				r_image=true;

				pingthread=new ServerPingThread(this);
				pingthread_ticket=Server->getThreadPool()->execute(pingthread);

				r_success=doImage(L"", 0, 0);
			}
			else if(!server_settings->getSettings()->no_images && ( isUpdateIncrImage() || do_incr_image_now) )
			{
				ScopedActiveThread sat;

				status.statusaction=sa_incr_image;
				ServerStatus::setServerStatus(status, true);

				ServerLogger::Log(clientid, "Doing incremental image backup...", LL_DEBUG);
				do_incr_image_now=false;
				r_image=true;
				r_incremental=true;
			
				pingthread=new ServerPingThread(this);
				pingthread_ticket=Server->getThreadPool()->execute(pingthread);

				SBackup last=getLastIncrementalImage();
				if(last.incremental==-2)
				{
					ServerLogger::Log(clientid, "Error retrieving last backup.", LL_ERROR);
					r_success=false;
				}
				else
				{
					r_success=doImage(last.path, last.incremental+1, last.incremental_ref);
				}
			}

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
				os_remove_nonempty_dir(backuppath);
			}

			status.action_done=false;
			status.statusaction=sa_none;
			status.pcdone=100;
			//Flush buffer before continuing...
			status.hashqueuesize=(_u32)hashpipe->getNumElements()+(bsh->isWorking()?1:0);
			status.prepare_hashqueuesize=(_u32)hashpipe_prepare->getNumElements()+(bsh_prepare->isWorking()?1:0);
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
				ServerLogger::Log(clientid, "Time taken for backing up client "+clientname+": "+nconvert(ptime), LL_INFO);
				if(!r_success)
				{
					ServerLogger::Log(clientid, "Backup not complete because of connection problems", LL_ERROR);
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
				ServerLogger::Log(clientid, "Time taken for creating image of client "+clientname+": "+nconvert(ptime), LL_INFO);
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
				saveClientLogdata(r_image?1:0, r_incremental?1:0);
				sendClientLogdata();
			}
			if(hbu)
			{
				ServerCleanupThread::updateStats();
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
		}
	}

	//destroy channel
	{
		Server->Log("Stopping channel...", LL_DEBUG);
		channel_thread.doExit();
		Server->getThreadPool()->waitFor(channel_thread_id);
	}

	if(do_exit_now)
	{
		hashpipe->Write("exitnow");
		std::string msg;
		exitpipe_prepare->Read(&msg);
		Server->destroy(exitpipe_prepare);
	}
	else
	{
		hashpipe->Write("exit");
	}
	
	
	Server->destroy(settings);
	Server->destroy(settings_client);
	delete server_settings;
	pipe->Write("ok");
	Server->Log("server_get Thread for client "+clientname+" finished");

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
	q_update_setting=db->Prepare("UPDATE settings SET value=? WHERE key=? AND clientid=?", false);
	q_insert_setting=db->Prepare("INSERT INTO settings (key, value, clientid) VALUES (?,?,?)", false);
	q_set_complete=db->Prepare("UPDATE backups SET complete=1 WHERE id=?", false);
	q_update_image_full=db->Prepare("SELECT id FROM backup_images WHERE datetime('now','-"+nconvert(s->update_freq_image_full)+" seconds')<backuptime AND clientid=? AND incremental=0 AND complete=1", false);
	q_update_image_incr=db->Prepare("SELECT id FROM backup_images WHERE datetime('now','-"+nconvert(s->update_freq_image_incr)+" seconds')<backuptime AND clientid=? AND complete=1", false); 
	q_create_backup_image=db->Prepare("INSERT INTO backup_images (clientid, path, incremental, incremental_ref, complete, running, size_bytes) VALUES (?, ?, ?, ?, 0, CURRENT_TIMESTAMP, 0)", false);
	q_set_image_size=db->Prepare("UPDATE backup_images SET size_bytes=? WHERE id=?", false);
	q_set_image_complete=db->Prepare("UPDATE backup_images SET complete=1 WHERE id=?", false);
	q_set_last_image_backup=db->Prepare("UPDATE clients SET lastbackup_image=CURRENT_TIMESTAMP WHERE id=?", false);
	q_get_last_incremental_image=db->Prepare("SELECT id,incremental,path FROM backup_images WHERE clientid=? AND incremental=0 AND complete=1 ORDER BY backuptime DESC LIMIT 1", false);
	q_update_running_file=db->Prepare("UPDATE backups SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	q_update_running_image=db->Prepare("UPDATE backup_images SET running=CURRENT_TIMESTAMP WHERE id=?", false);
	q_update_images_size=db->Prepare("UPDATE clients SET bytes_used_images=(SELECT bytes_used_images FROM clients WHERE id=?)+? WHERE id=?", false);
	q_set_done=db->Prepare("UPDATE backups SET done=1 WHERE id=?", false);
	q_save_logdata=db->Prepare("INSERT INTO logs (clientid, logdata, errors, warnings, infos, image, incremental) VALUES (?,?,?,?,?,?,?)", false);
	q_get_unsent_logdata=db->Prepare("SELECT id, strftime('%s', created) AS created, logdata FROM logs WHERE sent=0 AND clientid=?", false);
	q_set_logdata_sent=db->Prepare("UPDATE logs SET sent=1 WHERE id=?", false);
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
		IQuery *q_insert_newclient=db->Prepare("INSERT INTO clients (name, lastseen,bytes_used_files,bytes_used_images) VALUES (?, CURRENT_TIMESTAMP, 0, 0)", false);
		q_insert_newclient->Bind(clientname);
		q_insert_newclient->Write();
		int rid=(int)db->getLastInsertID();
		q_insert_newclient->Reset();
		db->destroyQuery(q_insert_newclient);
		return rid;
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

SBackup BackupServerGet::getLastIncrementalImage(void)
{
	q_get_last_incremental_image->Bind(clientid);
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

int BackupServerGet::createBackupImageSQL(int incremental, int incremental_ref, int clientid, std::wstring path)
{
	q_create_backup_image->Bind(clientid);
	q_create_backup_image->Bind(path);
	q_create_backup_image->Bind(incremental);
	q_create_backup_image->Bind(incremental_ref);
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

bool BackupServerGet::isUpdateFullImage(void)
{
	if(server_settings->getSettings()->update_freq_image_full<0)
		return false;

	q_update_image_full->Bind(clientid);
	db_results res=q_update_image_full->Read();
	q_update_image_full->Reset();
	return res.empty();
}

bool BackupServerGet::isUpdateIncrImage(void)
{
	if(server_settings->getSettings()->update_freq_image_full<=0)
		return false;

	q_update_image_incr->Bind(clientid);
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

bool BackupServerGet::request_filelist_construct(bool full)
{
	CTCPStack tcpstack;

	IPipe *cc=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, 10000);
	if(cc==NULL)
	{
		ServerLogger::Log(clientid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_ERROR);
		return false;
	}

	if(full)
		tcpstack.Send(cc, server_identity+"START FULL BACKUP");
	else
		tcpstack.Send(cc, server_identity+"START BACKUP");

	std::string ret;
	unsigned int starttime=Server->getTimeMS();
	while(Server->getTimeMS()-starttime<=full_backup_construct_timeout)
	{
		size_t rc=cc->Read(&ret, full_backup_construct_timeout);
		if(rc==0)
		{
			ServerLogger::Log(clientid, "Constructing of filelist of \""+clientname+"\" failed - TIMEOUT(1)", LL_ERROR);
			break;
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
				ServerLogger::Log(clientid, "Constructing of filelist of \""+clientname+"\" failed - TIMEOUT(2)", LL_ERROR);
				break;
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
	bool b=request_filelist_construct(true);
	if(!b)
	{
		has_error=true;
		return false;
	}

	FileClient fc;
	sockaddr_in addr=getClientaddr();
	_u32 rc=fc.Connect(&addr);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, "Full Backup of "+clientname+" failed - CONNECT error", LL_ERROR);
		has_error=true;
		return false;
	}
	
	IFile *tmp=Server->openTemporaryFile();

	rc=fc.GetFile("urbackup/filelist.ub", tmp);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, "Error getting filelist of "+clientname+". Errorcode: "+nconvert(rc), LL_ERROR);
		has_error=true;
		return false;
	}

	backupid=createBackupSQL(0, clientid, backuppath_single);
	
	tmp->Seek(0);
	
	resetEntryState();

	IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+".ub", MODE_WRITE);

	if(clientlist==NULL )
	{
		ServerLogger::Log(clientid, "Error creating clientlist for client "+clientname, LL_ERROR);
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

	while( (read=tmp->Read(buffer, 4096))>0 && r_done==false)
	{
		filelist_currpos+=read;
		for(size_t i=0;i<read;++i)
		{
			unsigned int ctime=Server->getTimeMS();
			if(ctime-laststatsupdate>status_update_intervall)
			{
				laststatsupdate=ctime;
				status.pcdone=(std::min)(100,(int)(((float)transferred)/((float)files_size/100.f)+0.5f));
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
						if(!os_create_dir(backuppath+os_curr_path))
						{
							ServerLogger::Log(clientid, L"Creating directory  \""+backuppath+os_curr_path+L"\" failed.", LL_ERROR);
						}
						++depth;
						if(depth==1)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							ServerLogger::Log(clientid, L"Starting shadowcopy \""+t+L"\".", LL_INFO);
							start_shadowcopy(wnarrow(t));
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
							stop_shadowcopy(wnarrow(t));
						}
						curr_path=ExtractFilePath(curr_path);
					}
				}
				else
				{
					bool b=load_file(cf.name, curr_path, fc);
					if(!b)
					{
						ServerLogger::Log(clientid, "Client "+clientname+" went offline.", LL_ERROR);
						r_done=true;
						break;
					}
					transferred+=cf.size;
				}
			}
		}
		clientlist->Write(buffer, read);
		if(read<4096)
			break;
	}
	if(r_done==false)
	{
		std::wstring backupfolder=server_settings->getSettings()->backupfolder;
		std::wstring currdir=backupfolder+os_file_sep()+widen(clientname)+os_file_sep()+L"current";
		Server->deleteFile(currdir);
		os_link_symbolic(backuppath, currdir);
	}
	running_updater->stop();
	updateRunning(false);
	Server->destroy(clientlist);
	Server->destroy(tmp);

	setBackupDone();

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
	
	_u32 rc=fc.GetFile(Server->ConvertToUTF8(cfn), fd);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, L"Error getting file \""+cfn+L"\" from "+widen(clientname)+L". Errorcode: "+convert(rc), LL_ERROR);
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
	bool b=request_filelist_construct(false);
	if(!b)
	{
		has_error=true;
		return false;
	}

	FileClient fc;
	sockaddr_in addr=getClientaddr();
	_u32 rc=fc.Connect(&addr);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, "Incremental Backup of "+clientname+" failed - CONNECT error", LL_ERROR);
		has_error=true;
		return false;
	}
	
	IFile *tmp=Server->openTemporaryFile();
	rc=fc.GetFile("urbackup/filelist.ub", tmp);
	if(rc!=ERR_SUCCESS)
	{
		ServerLogger::Log(clientid, "Error getting filelist of "+clientname+". Errorcode: "+nconvert(rc), LL_ERROR);
		has_error=true;
		return false;
	}

	SBackup last=getLastIncremental();
	if(last.incremental==-2)
	{
		ServerLogger::Log(clientid, "Error retrieving last backup.", LL_ERROR);
		has_error=true;
		return false;
	}
	backupid=createBackupSQL(last.incremental+1, clientid, backuppath_single);

	std::wstring backupfolder=server_settings->getSettings()->backupfolder;
	std::wstring last_backuppath=backupfolder+os_file_sep()+widen(clientname)+os_file_sep()+last.path;

	std::wstring tmpfilename=tmp->getFilenameW();
	Server->destroy(tmp);

	bool error=false;
	std::vector<size_t> diffs=TreeDiff::diffTrees("urbackup/clientlist_"+nconvert(clientid)+".ub", wnarrow(tmpfilename), error);
	if(error)
	{
		ServerLogger::Log(clientid, "Error while calculating tree diff.", LL_ERROR);
		has_error=true;
		return false;
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
	_i64 files_size=getIncrementalSize(tmp, diffs);
	tmp->Seek(0);
	_i64 transferred=0;
	
	unsigned int laststatsupdate=0;
	ServerStatus::setServerStatus(status, true);

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
					status.pcdone=(std::min)(100,(int)(((float)filelist_currpos)/((float)files_size/100.f)+0.5f));
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
						clientlist->Write("d\""+Server->ConvertToUTF8(cf.name)+"\"\n");
					}
					else if(cf.name==L".." && indir_currdepth>0)
					{
						--indir_currdepth;
						clientlist->Write("d\"..\"\n");
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
						if(!os_create_dir(backuppath+os_curr_path))
						{
							ServerLogger::Log(clientid, L"Creating directory  \""+backuppath+os_curr_path+L"\" failed.", LL_ERROR);
						}
						++depth;
						if(depth==1)
						{
							std::wstring t=curr_path;
							t.erase(0,1);
							if(r_offline==false)
							{
								start_shadowcopy(wnarrow(t));
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
								stop_shadowcopy(wnarrow(t));
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
								ServerLogger::Log(clientid, "Client "+clientname+" went offline.", LL_ERROR);
								r_offline=true;
							}
							else
							{
								transferred+=cf.size;
								clientlist->Write("f\""+Server->ConvertToUTF8(cf.name)+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
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

						bool b=os_create_hardlink(backuppath+os_curr_path, srcpath);
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
									ServerLogger::Log(clientid, "Client "+clientname+" went offline.", LL_ERROR);
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
							clientlist->Write("f\""+Server->ConvertToUTF8(cf.name)+"\" "+nconvert(cf.size)+" "+nconvert(cf.last_modified)+"\n");
						}
					}
				}
				++line;
			}
		}

		if(read<4096)
			break;
	}
	status.pcdone=100;
	ServerStatus::setServerStatus(status, true);
	Server->destroy(clientlist);
	if(r_offline==false)
	{
		Server->Log("Client ok. Copying full file...", LL_DEBUG);
		IFile *clientlist=Server->openFile("urbackup/clientlist_"+nconvert(clientid)+".ub", MODE_WRITE);
		if(clientlist!=NULL)
		{
			tmp->Seek(0);
			_u32 r=0;
			char buf[4096];
			do
			{
				r=tmp->Read(buf, 4096);
				if(clientlist->Write(buf, r)!=r)
				{
					ServerLogger::Log(clientid, "Fatal error copying clientlist. Write error.", LL_ERROR);
					break;
				}
			}
			while(r>0);
			Server->Log("Copying done.", LL_DEBUG);
		
			Server->Log("Creating symbolic links. -1", LL_DEBUG);

			std::wstring backupfolder=server_settings->getSettings()->backupfolder;
			std::wstring currdir=backupfolder+os_file_sep()+widen(clientname)+os_file_sep()+L"current";
			Server->deleteFile(currdir);
			os_link_symbolic(backuppath, currdir);

			Server->Log("Creating symbolic links. -2", LL_DEBUG);

			currdir=backupfolder+os_file_sep()+L"clients";
			if(!os_create_dir(currdir) && !os_directory_exists(currdir))
			{
				Server->Log("Error creating \"clients\" dir for symbolic links", LL_ERROR);
			}
			currdir+=os_file_sep()+widen(clientname);
			Server->deleteFile(currdir);
			os_link_symbolic(backuppath, currdir);

			Server->Log("Symbolic links created.", LL_DEBUG);

			Server->destroy(clientlist);
		}
		else
		{
			ServerLogger::Log(clientid, "Fatal error copying clientlist. Open error.", LL_ERROR);
		}
		Server->deleteFile("urbackup/clientlist_"+nconvert(clientid)+"_new.ub");
	}
	else
	{
		Server->Log("Client disconnected while backing up. Copying partial file...", LL_DEBUG);
		Server->deleteFile("urbackup/clientlist_"+nconvert(clientid)+".ub");
		moveFile(L"urbackup/clientlist_"+convert(clientid)+L"_new.ub", L"urbackup/clientlist_"+convert(clientid)+L".ub");
	}

	running_updater->stop();
	updateRunning(false);
	Server->destroy(tmp);
	Server->deleteFile(tmpfilename);

	setBackupDone();

	return !r_offline;
}

bool BackupServerGet::hasChange(size_t line, const std::vector<size_t> &diffs)
{
	return std::binary_search(diffs.begin(), diffs.end(), line);
}

void BackupServerGet::hashFile(std::wstring dstpath, IFile *fd)
{
	unsigned int l_backup_id=backupid;

	CWData data;
	data.addString(Server->ConvertToUTF8(fd->getFilenameW()));
	data.addUInt(l_backup_id);
	data.addString(Server->ConvertToUTF8(dstpath));

	ServerLogger::Log(clientid, "GT: Loaded file \""+ExtractFileName(Server->ConvertToUTF8(dstpath))+"\"", LL_DEBUG);

	Server->destroy(fd);
	hashpipe->Write(data.getDataPtr(), data.getDataSize() );
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
	backuppath=backupfolder+os_file_sep()+widen(clientname)+os_file_sep()+backuppath_single;
	return os_create_dir(backuppath);	
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
	return backupfolder_uncompr+os_file_sep()+widen(clientname)+os_file_sep()+L"Image_"+letter+L"_"+widen((std::string)buffer)+L".vhd";
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
	CTCPStack tcpstack;
	IPipe *cc=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, 10000);
	if(cc==NULL)
	{
		if(logerr)
			ServerLogger::Log(clientid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_ERROR);
		else
			Server->Log("Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_DEBUG);
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
			return ret;
		}
	}

	ServerLogger::Log(clientid, L"Timeout: "+errmsg, LL_ERROR);

	Server->destroy(cc);

	return "";
}

bool BackupServerGet::sendClientMessage(const std::string &msg, const std::string &retok, const std::wstring &errmsg, unsigned int timeout, bool logerr)
{
	CTCPStack tcpstack;
	IPipe *cc=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, 10000);
	if(cc==NULL)
	{
		if(logerr)
			ServerLogger::Log(clientid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_ERROR);
		else
			Server->Log("Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_DEBUG);
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
	sendClientMessage("START SC \""+path+"\"", "DONE", L"Activating shadow copy on \""+widen(clientname)+L"\" failed", shadow_copy_timeout);
}

void BackupServerGet::stop_shadowcopy(const std::string &path)
{
	sendClientMessage("STOP SC \""+path+"\"", "DONE", L"Removing shadow copy on \""+widen(clientname)+L"\" failed", shadow_copy_timeout);
}

void BackupServerGet::notifyClientBackupSuccessfull(void)
{
	sendClientMessage("DID BACKUP", "OK", L"Sending status to client failed", 10000);
}

void BackupServerGet::sendClientBackupIncrIntervall(void)
{
	sendClientMessage("INCRINTERVALL \""+nconvert(server_settings->getSettings()->update_freq_incr)+"\"", "OK", L"Sending incrintervall to client failed", 10000);
}

void BackupServerGet::sendSettings(void)
{
	std::string s_settings;

	std::vector<std::wstring> settings_names=getSettingsList();

	for(size_t i=0;i<settings_names.size();++i)
	{
		std::wstring &key=settings_names[i];
		std::wstring value;
		if(!settings_client->getValue(key, &value) )
			if(!settings->getValue(key, &value) )
				key=L"";

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
	FileClient fc;
	sockaddr_in addr=getClientaddr();
	_u32 rc=fc.Connect(&addr);
	if(rc!=ERR_CONNECTED)
	{
		ServerLogger::Log(clientid, "Getting Client settings of "+clientname+" failed - CONNECT error", LL_ERROR);
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
		ServerLogger::Log(clientid, "Error getting Client settings of "+clientname+". Errorcode: "+nconvert(rc), LL_ERROR);
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
	Server->deleteFile(tmp_fn);
	Server->destroy(tmp);

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
		sendClientMessage("2LOGDATA "+wnarrow(res[i][L"created"])+" "+logdata, "OK", L"Sending logdata to client failed", 10000);
		q_set_logdata_sent->Bind(res[i][L"id"]);
		q_set_logdata_sent->Write();
		q_set_logdata_sent->Reset();
	}
}

void BackupServerGet::saveClientLogdata(int image, int incremental)
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

	ServerLogger::reset(clientid);
}

const unsigned int stat_update_skip=20;
const unsigned int sector_size=512;
const unsigned int sha_size=32;

void writeZeroblockdata(void)
{
	const int64 vhd_blocksize=(1024*1024/4);
	unsigned char *zeroes=new unsigned char[vhd_blocksize];
	memset(zeroes, 0, vhd_blocksize);
	unsigned char dig[sha_size];
	sha256(zeroes, vhd_blocksize, dig);
	IFile *out=Server->openFile("zero.hash", MODE_WRITE);
	out->Write((char*)dig, sha_size);
	Server->destroy(out);
	delete []zeroes;
}

bool BackupServerGet::doImage(const std::wstring &pParentvhd, int incremental, int incremental_ref)
{
	CTCPStack tcpstack;
	IPipe *cc=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, 10000);
	if(cc==NULL)
	{
		ServerLogger::Log(clientid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_ERROR);
		return false;
	}

	if(pParentvhd.empty())
	{
		tcpstack.Send(cc, server_identity+"FULL IMAGE letter=C:");
	}
	else
	{
		IFile *hashfile=Server->openFile(pParentvhd+L".hash");
		if(hashfile==NULL)
		{
			ServerLogger::Log(clientid, "Error opening hashfile", LL_ERROR);
			Server->destroy(cc);
			return false;
		}
		std::string ts=server_identity+"INCR IMAGE letter=C:&hashsize="+nconvert(hashfile->Size());
		size_t rc=tcpstack.Send(cc, ts);
		if(rc==0)
		{
			ServerLogger::Log(clientid, "Sending 'INCR IMAGE' command failed", LL_ERROR);
			Server->destroy(cc);
			Server->destroy(hashfile);
			return false;
		}
		hashfile->Seek(0);
		char buffer[4096];
		for(size_t i=0,hsize=(size_t)hashfile->Size();i<hsize;i+=4096)
		{
			size_t tsend=(std::min)((size_t)4096, hsize-i);
			if(hashfile->Read(buffer, (_u32)tsend)!=tsend)
			{
				ServerLogger::Log(clientid, "Reading from hashfile failed", LL_ERROR);
				Server->destroy(cc);
				Server->destroy(hashfile);
				return false;
			}
			if(!cc->Write(buffer, tsend))
			{
				ServerLogger::Log(clientid, "Sending hashdata failed", LL_ERROR);
				Server->destroy(cc);
				Server->destroy(hashfile);
				return false;
			}
		}
		Server->destroy(hashfile);
	}

	std::wstring imagefn=constructImagePath(L"C");

	{
		std::string mbrd=getMBR(L"C");
		if(mbrd.empty())
		{
			ServerLogger::Log(clientid, "Error getting MBR data", LL_ERROR);
		}
		else
		{
			IFile *mbr_file=Server->openFile(imagefn+L".mbr", MODE_WRITE);
			mbr_file->Write(mbrd);
			Server->destroy(mbr_file);
		}
	}

	int64 free_space=os_free_space(ExtractFilePath(imagefn));
	if(free_space!=-1 && free_space<minfreespace_image)
	{
		ServerLogger::Log(clientid, "Not enough free space. Cleaning up.", LL_INFO);
		ServerCleanupThread cleanup;
		if(!cleanup.do_cleanup(minfreespace_image) )
		{
			ServerLogger::Log(clientid, "Could not free space for image. NOT ENOUGH FREE SPACE.", LL_ERROR);
			return false;
		}
	}

	if(pParentvhd.empty())
		backupid=createBackupImageSQL(0,0, clientid, imagefn);
	else
		backupid=createBackupImageSQL(incremental, incremental_ref, clientid, imagefn);


	std::string ret;
	unsigned int starttime=Server->getTimeMS();
	bool first=true;
	char buffer[4096];
	unsigned int blocksize;
	unsigned int blockleft=0;
	int64 currblock=-1;
	char *blockdata=NULL;
	int64 drivesize;
	ServerVHDWriter *vhdfile=NULL;
	THREADPOOL_TICKET vhdfile_ticket;
	IVHDFile *r_vhdfile=NULL;
	IFile *hashfile=NULL;
	IFile *parenthashfile=NULL;
	int64 blockcnt=0;
	int64 numblocks=0;
	int64 totalblocks=0;
	int64 mbr_offset=0;
	_u32 off=0;
	std::string shadowdrive;
	int shadow_id=-1;
	bool persistent=false;
	unsigned char *zeroblockdata=NULL;
	int64 nextblock=0;
	int64 vhd_blocksize=(1024*1024)/4;
	ServerRunningUpdater *running_updater=new ServerRunningUpdater(backupid, true);
	Server->getThreadPool()->execute(running_updater);

	bool has_parent=false;
	if(!pParentvhd.empty())
		has_parent=true;

	sha256_ctx shactx;
	sha256_init(&shactx);

	unsigned int stat_update_cnt=0;

	while(Server->getTimeMS()-starttime<=3600000)
	{
		size_t r=cc->Read(&buffer[off], 4096-off, 3600000);
		if(r!=0)
			r+=off;
		starttime=Server->getTimeMS();
		off=0;
		if(r==0 )
		{
			if(persistent && nextblock!=0)
			{
				while(true)
				{
					ServerStatus::setROnline(clientname, false);
					if(cc!=NULL)
						Server->destroy(cc);
					Server->Log("Trying to reconnect in doImage", LL_DEBUG);
					cc=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, 10000);
					if(cc==NULL)
					{
						std::string msg;
						if(pipe->Read(&msg, 0)>0)
						{
							if(msg.find("address")==0)
							{
								IScopedLock lock(clientaddr_mutex);
								memcpy(&clientaddr, &msg[7], sizeof(sockaddr_in) );
							}
							else
							{
								pipe->Write(msg);
							}
							Server->wait(60000);
						}
					}
					else
					{
						ServerStatus::setROnline(clientname, true);
						Server->Log("Reconnected.", LL_DEBUG);
						break;
					}
				}

				if(pParentvhd.empty())
				{
					tcpstack.Send(cc, server_identity+"FULL IMAGE letter=C:&shadowdrive="+shadowdrive+"&start="+nconvert(nextblock)+"&shadowid="+nconvert(shadow_id));
				}
				else
				{
					std::string ts="INCR IMAGE letter=C:&shadowdrive="+shadowdrive+"&start="+nconvert(nextblock)+"&shadowid="+nconvert(shadow_id)+"&hashsize="+nconvert(parenthashfile->Size());
					size_t rc=tcpstack.Send(cc, server_identity+ts);
					if(rc==0)
					{
						ServerLogger::Log(clientid, "Sending 'INCR IMAGE' command failed", LL_ERROR);
						Server->destroy(cc);
						if(vhdfile!=NULL)
						{
							vhdfile->freeBuffer(blockdata);
							vhdfile->doExitNow();
							Server->getThreadPool()->waitFor(vhdfile_ticket);
							delete vhdfile;
							vhdfile=NULL;
						}
						if(hashfile!=NULL) Server->destroy(hashfile);
						if(parenthashfile!=NULL) Server->destroy(parenthashfile);
						running_updater->stop();
						return false;
					}
					parenthashfile->Seek(0);
					char buffer[4096];
					for(size_t i=0,hsize=(size_t)parenthashfile->Size();i<hsize;i+=4096)
					{
						size_t tsend=(std::min)((size_t)4096, hsize-i);
						if(parenthashfile->Read(buffer, (_u32)tsend)!=tsend)
						{
							ServerLogger::Log(clientid, "Reading from hashfile failed i="+nconvert(i), LL_ERROR);
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								Server->getThreadPool()->waitFor(vhdfile_ticket);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							return false;
						}
						if(!cc->Write(buffer, tsend))
						{
							ServerLogger::Log(clientid, "Sending hashdata failed", LL_ERROR);
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								Server->getThreadPool()->waitFor(vhdfile_ticket);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							return false;
						}
					}

				}
				off=0;
				starttime=Server->getTimeMS();

				blockleft=0;
				currblock=-1;
			}
			else
			{
				ServerLogger::Log(clientid, "Pipe to client unexpectedly closed", LL_ERROR);
				Server->destroy(cc);
				if(vhdfile!=NULL)
				{
					vhdfile->freeBuffer(blockdata);
					vhdfile->doExitNow();
					Server->getThreadPool()->waitFor(vhdfile_ticket);
					delete vhdfile;
					vhdfile=NULL;
				}
				if(hashfile!=NULL) Server->destroy(hashfile);
				if(parenthashfile!=NULL) Server->destroy(parenthashfile);
				running_updater->stop();
				return false;
			}
		}
		else
		{
			if(first)
			{
				first=false;
				if(r>=sizeof(unsigned int))
				{
					memcpy(&blocksize, buffer, sizeof(unsigned int) );
					off+=sizeof(unsigned int);
					vhd_blocksize/=blocksize;
				}
				if(blocksize==0xFFFFFFFF)
				{
					off+=sizeof(unsigned int);
					if(r>sizeof(uint64))
					{
						std::string err;
						err.resize(r-sizeof(uint64) );
						memcpy(&err[0], &buffer[off], r-off);
						ServerLogger::Log(clientid, "Request of image backup failed. Reason: "+err, LL_ERROR);
					}
					else
					{
						ServerLogger::Log(clientid, "Error on client. No reason given.", LL_ERROR);
					}
					Server->destroy(cc);
					
					if(vhdfile!=NULL)
					{
						vhdfile->freeBuffer(blockdata);
						vhdfile->doExitNow();
						Server->getThreadPool()->waitFor(vhdfile_ticket);
						delete vhdfile;
						vhdfile=NULL;
					}
					if(hashfile!=NULL) Server->destroy(hashfile);
					if(parenthashfile!=NULL) Server->destroy(parenthashfile);
					running_updater->stop();
					return false;
				}
				bool issmall=false;
				if(r>=sizeof(unsigned int)+sizeof(int64))
				{
					memcpy(&drivesize, &buffer[off], sizeof(int64) );
					off+=sizeof(int64);

					blockcnt=drivesize/blocksize;
					totalblocks=blockcnt;

					zeroblockdata=new unsigned char[blocksize];
					memset(zeroblockdata, 0, blocksize);

					if(!has_parent)
						r_vhdfile=image_fak->createVHDFile(imagefn, false, drivesize+(int64)mbr_size, (unsigned int)vhd_blocksize*blocksize);
					else
						r_vhdfile=image_fak->createVHDFile(imagefn, pParentvhd, false);

					if(r_vhdfile==NULL)
					{
						ServerLogger::Log(clientid, L"Error opening VHD file \""+imagefn+L"\"", LL_ERROR);
						Server->destroy(cc);
						if(vhdfile!=NULL)
						{
							vhdfile->freeBuffer(blockdata);
							vhdfile->doExitNow();
							Server->getThreadPool()->waitFor(vhdfile_ticket);
							delete vhdfile;
							vhdfile=NULL;
						}
						if(hashfile!=NULL) Server->destroy(hashfile);
						if(parenthashfile!=NULL) Server->destroy(parenthashfile);
						running_updater->stop();
						return false;
					}

					vhdfile=new ServerVHDWriter(r_vhdfile, blocksize, 5000, clientid);
					vhdfile_ticket=Server->getThreadPool()->execute(vhdfile);

					blockdata=vhdfile->getBuffer();

					hashfile=Server->openFile(imagefn+L".hash", MODE_WRITE);
					if(hashfile==NULL)
					{
						ServerLogger::Log(clientid, L"Error opening Hashfile \""+imagefn+L".hash\"", LL_ERROR);
						Server->destroy(cc);
						if(vhdfile!=NULL)
						{
							vhdfile->freeBuffer(blockdata);
							vhdfile->doExitNow();
							Server->getThreadPool()->waitFor(vhdfile_ticket);
							delete vhdfile;
							vhdfile=NULL;
						}
						if(hashfile!=NULL) Server->destroy(hashfile);
						if(parenthashfile!=NULL) Server->destroy(parenthashfile);
						running_updater->stop();
						return false;
					}

					if(has_parent)
					{
						parenthashfile=Server->openFile(pParentvhd+L".hash", MODE_READ);
						if(parenthashfile==NULL)
						{
							ServerLogger::Log(clientid, L"Error opening Parenthashfile \""+pParentvhd+L".hash\"", LL_ERROR);
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								Server->getThreadPool()->waitFor(vhdfile_ticket);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							return false;
						}
					}

					mbr_offset=writeMBR(vhdfile, drivesize);
				}
				else
				{
					issmall=true;
				}
				if(r>=sizeof(unsigned int)+sizeof(int64)+sizeof(int64))
				{
					memcpy(&blockcnt, &buffer[off], sizeof(int64) );
					off+=sizeof(int64);
				}
				else
				{
					issmall=true;
				}
				if(r>=sizeof(unsigned int)+sizeof(int64)+sizeof(int64)+1)
				{
					char c_persistent=buffer[off];
					if(c_persistent!=0)
						persistent=true;
					++off;
				}
				else
				{
					issmall=true;
				}

				unsigned int shadowdrive_size=0;
				if(r>=sizeof(unsigned int)+sizeof(int64)+sizeof(int64)+1+sizeof(unsigned int))
				{
					memcpy(&shadowdrive_size, &buffer[off], sizeof(unsigned int));
					off+=sizeof(unsigned int);
					if(shadowdrive_size>0)
					{
						if( r>=sizeof(unsigned int)+sizeof(int64)+sizeof(int64)+1+sizeof(unsigned int)+shadowdrive_size)
						{
							shadowdrive.resize(shadowdrive_size);
							memcpy(&shadowdrive[0],  &buffer[off], shadowdrive_size);
							off+=shadowdrive_size;
						}
						else
						{
							issmall=true;
						}
					}
				}
				else
				{
					issmall=true;
				}

				if(r>=sizeof(unsigned int)+sizeof(int64)+sizeof(int64)+1+sizeof(unsigned int)+shadowdrive_size+sizeof(int))
				{
					memcpy(&shadow_id, &buffer[off], sizeof(int));
					off+=sizeof(int);
				}
				else
				{
					issmall=true;
				}

				if(issmall)
				{
					ServerLogger::Log(clientid, "First packet to small", LL_ERROR);
					Server->destroy(cc);
					if(vhdfile!=NULL)
					{
						vhdfile->freeBuffer(blockdata);
						vhdfile->doExitNow();
						Server->getThreadPool()->waitFor(vhdfile_ticket);
						delete vhdfile;
						vhdfile=NULL;
					}
					if(parenthashfile!=NULL) Server->destroy(parenthashfile);
					if(hashfile!=NULL) Server->destroy(hashfile);
					running_updater->stop();
					return false;
				}

				if(r==off)
				{
					off=0;
					continue;
				}
			}
			while(true)
			{
				if(blockleft==0)
				{
					if(currblock!=-1) // write current block
					{
						++numblocks;
						++stat_update_cnt;
						if(stat_update_cnt%stat_update_skip==0)
						{
							stat_update_cnt=0;
							if(blockcnt!=0)
							{
								if(has_parent)
								{
									status.pcdone=(int)(((double)currblock/(double)totalblocks)*100.0+0.5);
								}
								else
								{
									status.pcdone=(int)(((double)numblocks/(double)blockcnt)*100.0+0.5);
								}
								ServerStatus::setServerStatus(status, true);
							}
						}

						nextblock=updateNextblock(nextblock, currblock, &shactx, zeroblockdata, has_parent, vhdfile, hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize);
						sha256_update(&shactx, (unsigned char *)blockdata, blocksize);

						vhdfile->writeBuffer(mbr_offset+currblock*blocksize, blockdata, blocksize);
						blockdata=vhdfile->getBuffer();

						if(nextblock%vhd_blocksize==0 && nextblock!=0)
						{
							//Server->Log("Hash written "+nconvert(currblock), LL_DEBUG);
							unsigned char dig[sha_size];
							sha256_final(&shactx, dig);
							hashfile->Write((char*)dig, sha_size);
							sha256_init(&shactx);
						}

						if(vhdfile->hasError())
						{
							ServerLogger::Log(clientid, "FATAL ERROR: Could not write to VHD-File", LL_ERROR);
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								std::vector<THREADPOOL_TICKET> wf;wf.push_back(vhdfile_ticket);
								Server->getThreadPool()->waitFor(wf);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							return false;
						}

						currblock=-1;
					}
					
					if(r-off>=sizeof(int64) )
					{
						memcpy(&currblock, &buffer[off], sizeof(int64) );
						if(currblock==-123)
						{
							int64 t_totalblocks=totalblocks;
							if(t_totalblocks%vhd_blocksize!=0)
								t_totalblocks+=vhd_blocksize-t_totalblocks%vhd_blocksize;

							nextblock=updateNextblock(nextblock, t_totalblocks, &shactx, zeroblockdata, has_parent, vhdfile, hashfile, parenthashfile, blocksize, mbr_offset, vhd_blocksize);

							if(nextblock%vhd_blocksize==0 && nextblock!=0)
							{
								//Server->Log("Hash written "+nconvert(nextblock), LL_INFO);
								unsigned char dig[sha_size];
								sha256_final(&shactx, dig);
								hashfile->Write((char*)dig, sha_size);
							}

							Server->destroy(cc);
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
							}
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);

							bool vhdfile_err=false;

							status.action_done=true;
							ServerStatus::setServerStatus(status);

							if(vhdfile!=NULL)
							{
								if(pingthread!=NULL)
								{
									pingthread->setStop(true);
									Server->getThreadPool()->waitFor(pingthread_ticket);
									pingthread=NULL;
								}
								vhdfile->doExit();
								Server->getThreadPool()->waitFor(vhdfile_ticket);
								vhdfile_err=vhdfile->hasError();
								delete vhdfile;
								vhdfile=NULL;
							}

							IFile *t_file=Server->openFile(imagefn, MODE_READ);
							db->BeginTransaction();
							q_set_image_size->Bind(t_file->Size());
							q_set_image_size->Bind(backupid);
							q_set_image_size->Write();
							q_set_image_size->Reset();
							q_update_images_size->Bind(clientid);
							q_update_images_size->Bind(t_file->Size());
							q_update_images_size->Bind(clientid);
							q_update_images_size->Write();
							q_update_images_size->Reset();
							setBackupImageComplete();
							db->EndTransaction();
							Server->destroy(t_file);

							running_updater->stop();
							updateRunning(true);

							return !vhdfile_err;
						}
						else if(currblock==-124 ||
#ifndef _WIN32 
								currblock==0xFFFFFFFFFFFFFFFFLLU)
#else
								currblock==0xFFFFFFFFFFFFFFFF)
#endif
						{
							if(r-off>sizeof(int64))
							{
								std::string err;
								err.resize(r-off-sizeof(int64) );
								memcpy(&err[0], &buffer[off+sizeof(int64)], r-off-sizeof(int64));
								ServerLogger::Log(clientid, "Error on client occured: "+err, LL_ERROR);
							}
							Server->destroy(cc);
							if(vhdfile!=NULL)
							{
								vhdfile->freeBuffer(blockdata);
								vhdfile->doExitNow();
								std::vector<THREADPOOL_TICKET> wf;wf.push_back(vhdfile_ticket);
								Server->getThreadPool()->waitFor(wf);
								delete vhdfile;
								vhdfile=NULL;
							}
							if(hashfile!=NULL) Server->destroy(hashfile);
							if(parenthashfile!=NULL) Server->destroy(parenthashfile);
							running_updater->stop();
							return false;
						}
						else if(currblock==-125) //ping
						{
							off+=sizeof(int64);
							currblock=-1;
						}
						else
						{
							off+=sizeof(int64);
							blockleft=blocksize;
						}
					}
					else if(r-off>0)
					{
						char buf2[4096];
						memcpy(buf2, &buffer[off], r-off);
						memcpy(buffer, buf2, r-off);
						off=(_u32)r-off;
						break;
					}
					else
					{
						off=0;
						break;
					}
				}
				else
				{
					unsigned int available=(std::min)(blockleft, (unsigned int)r-off);
					memcpy(&blockdata[blocksize-blockleft], &buffer[off], available);
					blockleft-=available;
					off+=available;
					if( off>=r )
					{
						off=0;
						break;
					}
				}
			}
		}
	}
	ServerLogger::Log(clientid, "Timeout while transfering image data", LL_ERROR);
	Server->destroy(cc);
	if(vhdfile!=NULL)
	{
		vhdfile->freeBuffer(blockdata);
		vhdfile->doExitNow();
		std::vector<THREADPOOL_TICKET> wf;wf.push_back(vhdfile_ticket);
		Server->getThreadPool()->waitFor(wf);
		delete vhdfile;
		vhdfile=NULL;
	}
	if(hashfile!=NULL) Server->destroy(hashfile);
	if(parenthashfile!=NULL) Server->destroy(parenthashfile);
	running_updater->stop();
	return false;
}

unsigned int BackupServerGet::writeMBR(ServerVHDWriter *vhdfile, uint64 volsize)
{
	unsigned char *mbr=(unsigned char *)vhdfile->getBuffer();
	unsigned char *mptr=mbr;

	memcpy(mptr, mbr_code, 440);
	mptr+=440;
	int sig=rand();
	memcpy(mptr, &sig, sizeof(int));
	mptr+=sizeof(int);
	*mptr=0;
	++mptr;
	*mptr=0;
	++mptr;

	unsigned char partition[16];
	partition[0]=0x80;
	partition[1]=0xfe;
	partition[2]=0xff;
	partition[3]=0xff;
	partition[4]=0x07; //ntfs
	partition[5]=0xfe;
	partition[6]=0xff;
	partition[7]=0xff;
	partition[8]=0x00;
	partition[9]=0x02;
	partition[10]=0x00;
	partition[11]=0x00;

	unsigned int sectors=(unsigned int)(volsize/((uint64)sector_size));

	memcpy(&partition[12], &sectors, sizeof(unsigned int) );

	memcpy(mptr, partition, 16);
	mptr+=16;
	for(int i=0;i<3;++i)
	{
		memset(mptr, 0, 16);
		mptr+=16;
	}
	*mptr=0x55;
	++mptr;
	*mptr=0xaa;
	vhdfile->writeBuffer(0, (char*)mbr, 512);

	for(int i=0;i<511;++i)
	{
		char *buf=vhdfile->getBuffer();
		memset(buf, 0, 512);
		vhdfile->writeBuffer((i+1)*512, buf, 512);
	}

	return 512*512;
}

int64 BackupServerGet::updateNextblock(int64 nextblock, int64 currblock, sha256_ctx *shactx, unsigned char *zeroblockdata, bool parent_fn, ServerVHDWriter *parentfile, IFile *hashfile, IFile *parenthashfile, unsigned int blocksize, int64 mbr_offset, int64 vhd_blocksize)
{
	unsigned char *blockdata=NULL;
	if(parent_fn)
		blockdata=new unsigned char[blocksize];

	if(currblock-nextblock>vhd_blocksize)
	{
		while(true)
		{
			if(!parent_fn)
			{
				sha256_update(shactx, zeroblockdata, blocksize);
			}
			else
			{
				{
					IScopedLock lock(parentfile->getVHDMutex());
					IVHDFile *vhd=parentfile->getVHD();
					vhd->Seek(mbr_offset+nextblock*blocksize);
					size_t read;
					bool b=vhd->Read((char*)blockdata, blocksize, read);
					if(!b)
						Server->Log("Reading from VHD failed", LL_ERROR);
				}
				sha256_update(shactx, blockdata, blocksize);
			}

			++nextblock;

			if(nextblock%vhd_blocksize==0)
			{
				unsigned char dig[sha_size];
				sha256_final(shactx, dig);
				hashfile->Write((char*)dig, sha_size);
				sha256_init(shactx);
				break;
			}
		}

		while(currblock-nextblock>vhd_blocksize)
		{
			if(!parent_fn)
			{
				hashfile->Write((char*)zero_hash, sha_size);
			}
			else
			{
				bool b=parenthashfile->Seek((nextblock/vhd_blocksize)*sha_size);
				if(!b)
				{
					Server->Log("Seeking in parenthashfile failed", LL_ERROR);
				}
				char dig[sha_size];
				_u32 rc=parenthashfile->Read(dig, sha_size);
				if(rc!=sha_size)
					Server->Log("Writing to parenthashfile failed", LL_ERROR);
				hashfile->Write(dig, sha_size);
			}
			nextblock+=vhd_blocksize;
		}
	}

	while(nextblock<currblock)
	{
		if(!parent_fn)
		{
			sha256_update(shactx, zeroblockdata, blocksize);
		}
		else
		{
			{
				IScopedLock lock(parentfile->getVHDMutex());
				IVHDFile *vhd=parentfile->getVHD();
				vhd->Seek(mbr_offset+nextblock*blocksize);
				size_t read;
				bool b=vhd->Read((char*)blockdata, blocksize, read);
				if(!b)
					Server->Log("Reading from VHD failed", LL_ERROR);
			}
			sha256_update(shactx, blockdata, blocksize);
		}
		++nextblock;
		if(nextblock%vhd_blocksize==0 && nextblock!=0)
		{
			unsigned char dig[sha_size];
			sha256_final(shactx, dig);
			hashfile->Write((char*)dig, sha_size);
			sha256_init(shactx);
		}
	}
	delete [] blockdata;
	return nextblock+1;
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
	else
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

			CTCPStack tcpstack;
			IPipe *cc=Server->ConnectStream(inet_ntoa(getClientaddr().sin_addr), serviceport, 10000);
			if(cc==NULL)
			{
				ServerLogger::Log(clientid, "Connecting to ClientService of \""+clientname+"\" failed - CONNECT error", LL_ERROR);
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

#endif //CLIENT_ONLY