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

#include "ClientService.h"
#include "RestoreFiles.h"
#include "client.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/File.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"
#include "database.h"
#include "../common/data.h"
#include "../fsimageplugin/IFilesystem.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "../urbackupcommon/sha2/sha2.h"
#include "ServerIdentityMgr.h"
#include "../urbackupcommon/settings.h"
#include "ImageThread.h"
#include "InternetClient.h"
#include "../urbackupcommon/settingslist.h"
#include "../urbackupcommon/capa_bits.h"

#include <memory.h>
#include <stdlib.h>
#include <limits.h>
#include <memory>
#include <algorithm>
#include <assert.h>


#ifndef _WIN32
#define _atoi64 atoll
#endif

#ifdef _WIN32
#define UPDATE_FILE_PREFIX ""
#define UPDATE_SIGNATURE_PREFIX ""
#else
#define UPDATE_FILE_PREFIX "urbackup/"
#include "../config.h"
#define UPDATE_SIGNATURE_PREFIX DATADIR "/urbackup/"
#endif

extern ICryptoFactory *crypto_fak;
extern std::string time_format_str;

ICustomClient* ClientService::createClient()
{
	return new ClientConnector();
}

void ClientService::destroyClient( ICustomClient * pClient)
{
	delete ((ClientConnector*)pClient);
}

std::vector<SRunningProcess> ClientConnector::running_processes;
std::vector<SFinishedProcess> ClientConnector::finished_processes;
int64 ClientConnector::curr_backup_running_id = 0;
IMutex *ClientConnector::backup_mutex=NULL;
IMutex *ClientConnector::process_mutex = NULL;
int ClientConnector::backup_interval=6*60*60;
int ClientConnector::backup_alert_delay=5*60;
std::vector<SChannel> ClientConnector::channel_pipes;
db_results ClientConnector::cached_status;
std::map<std::string, int64> ClientConnector::last_token_times;
int ClientConnector::last_capa=0;
IMutex *ClientConnector::ident_mutex=NULL;
std::vector<std::string> ClientConnector::new_server_idents;
bool ClientConnector::end_to_end_file_backup_verification_enabled=false;
std::map<std::pair<std::string, std::string>, std::string> ClientConnector::challenges;
bool ClientConnector::has_file_changes = false;
std::vector < ClientConnector::SFilesrvConnection > ClientConnector::fileserv_connections;
RestoreOkStatus ClientConnector::restore_ok_status = RestoreOk_None;
bool ClientConnector::status_updated= false;
RestoreFiles* ClientConnector::restore_files = NULL;
size_t ClientConnector::needs_restore_restart = 0;
size_t ClientConnector::ask_restore_ok = 0;
int64 ClientConnector::service_starttime = 0;
SRestoreToken ClientConnector::restore_token;
std::map<std::string, SAsyncFileList> ClientConnector::async_file_index;


#ifdef _WIN32
SVolumesCache* ClientConnector::volumes_cache;
#endif


#ifdef _WIN32
const std::string pw_file="pw.txt";
const std::string pw_change_file="pw_change.txt";
#else
const std::string pw_file="urbackup/pw.txt";
const std::string pw_change_file="urbackup/pw_change.txt";
#endif

namespace
{
	class UpdateSilentThread : public IThread
	{
	public:
		void operator()(void)
		{
			Server->wait(2 * 60 * 1000); //2min

#ifdef _WIN32
			STARTUPINFOW si;
			PROCESS_INFORMATION pi;
			memset(&si, 0, sizeof(STARTUPINFO));
			memset(&pi, 0, sizeof(PROCESS_INFORMATION));
			si.cb = sizeof(STARTUPINFO);
			if (!CreateProcessW(L"UrBackupUpdate.exe", L"UrBackupUpdate.exe /S", NULL, NULL, false, NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
			{
				Server->Log("Executing silent update failed: " + convert((int)GetLastError()), LL_ERROR);
			}
			else
			{
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
			}
#else
			system("/bin/sh urbackup/UrBackupUpdate.sh -- silent");
#endif

			delete this;
		}
	};

	class TimeoutFilesrvThread : public IThread
	{
	public:
		void operator()()
		{
			while (true)
			{
				Server->wait(600000);
				ClientConnector::timeoutFilesrvConnections();
			}
		}
	};
}

void ClientConnector::init_mutex(void)
{
	if(backup_mutex==NULL)
	{
		backup_mutex=Server->createMutex();
		ident_mutex=Server->createMutex();
		process_mutex = Server->createMutex();

		Server->createThread(new TimeoutFilesrvThread, "filesrv timeout");
	}
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	db_results res=db->Read("SELECT tvalue FROM misc WHERE tkey='last_capa'");
	if(!res.empty())
	{
		last_capa=watoi(res[0]["last_capa"]);
	}
	else
	{
		db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('last_capa', '0');");
		last_capa=0;
	}

	ClientDAO clientdao(db);

	std::string tmp = clientdao.getMiscValue("backup_alert_delay");
	if(!tmp.empty())
	{
		backup_alert_delay = watoi(tmp);
	}
	tmp = clientdao.getMiscValue("backup_interval");
	if(!tmp.empty())
	{
		backup_interval = watoi(tmp);
	}

	service_starttime = Server->getTimeMS();
}

void ClientConnector::destroy_mutex(void)
{
	Server->destroy(backup_mutex);
	Server->destroy(ident_mutex);
	Server->destroy(process_mutex);
}

ClientConnector::ClientConnector(void)
{
	mempipe=NULL;
}

bool ClientConnector::wantReceive(void)
{
	return want_receive;
}

void ClientConnector::Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpointName)
{
	tid=pTID;
	pipe=pPipe;
	state=CCSTATE_NORMAL;
	image_inf.thread_action=TA_NONE;
	image_inf.image_thread=NULL;
	image_inf.clientsubname.clear();
	if(mempipe==NULL)
	{
		mempipe=Server->createMemoryPipe();
		mempipe_owner=true;
	}
	lasttime=Server->getTimeMS();
	do_quit=false;
	is_channel=false;
	want_receive=true;
	last_channel_ping=0;
	file_version=1;
	internet_conn=false;
	tcpstack.setAddChecksum(false);
	last_update_time=lasttime;
	endpoint_name = pEndpointName;
	make_fileserv=false;
	local_backup_running_id = 0;
	run_other = NULL;
	idle_timeout = 10000;
	bitmapfile = NULL;
}

ClientConnector::~ClientConnector(void)
{
	if(mempipe_owner)
	{
		if(mempipe!=NULL)
		{
			Server->destroy(mempipe);
		}
	}
	else
	{
		mempipe->Write("exit");
	}
}

bool ClientConnector::Run(IRunOtherCallback* p_run_other)
{
	run_other = p_run_other;

	if(do_quit)
	{
		if(state!=CCSTATE_START_FILEBACKUP 
			&& state!=CCSTATE_START_FILEBACKUP_ASYNC
			&& state!=CCSTATE_SHADOWCOPY 
			&& state!=CCSTATE_WAIT_FOR_CONTRACTORS)
		{
			if(is_channel)
			{
				IScopedLock lock(backup_mutex);
				for(size_t i=0;i<channel_pipes.size();++i)
				{
					if(channel_pipes[i].pipe==pipe)
					{
						channel_pipes.erase(channel_pipes.begin()+i);
						break;
					}
				}
			}
			if(waitForThread())
			{
				want_receive = false;
				return true;
			}
			delete image_inf.image_thread;
			image_inf.image_thread=NULL;
			return false;
		}
		else
		{
			want_receive=false;
		}
	}

	switch(state)
	{
	case CCSTATE_NORMAL:
		if(Server->getTimeMS()-lasttime>idle_timeout)
		{
			Server->Log("Client timeout in ClientConnector::Run", LL_DEBUG);
			if(waitForThread())
			{
				do_quit=true;
				return true;
			}
			return false;
		}
		return true;
	case CCSTATE_START_FILEBACKUP_ASYNC:
	case CCSTATE_START_FILEBACKUP:
		{
			std::string msg;
			mempipe->Read(&msg, 0);

			if (state == CCSTATE_START_FILEBACKUP_ASYNC)
			{
				IScopedLock lock(backup_mutex);
				std::map<std::string, SAsyncFileList>::iterator it = async_file_index.find(async_file_list_id);
				if (it != async_file_index.end())
				{
					if (!msg.empty())
					{
						async_file_index.erase(it);
					}
					else
					{
						it->second.last_update = Server->getTimeMS();
					}
				}
			}

			if(msg=="exit")
			{
				mempipe->Write("exit");
				mempipe=Server->createMemoryPipe();
				mempipe_owner=true;
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				IScopedLock lock(backup_mutex);
				removeRunningProcess(local_backup_running_id, false);
				return false;
			}
			else if(msg=="done")
			{
				mempipe->Write("exit");
				mempipe=Server->createMemoryPipe();
				mempipe_owner=true;
				tcpstack.Send(pipe, "DONE");
				lasttime=Server->getTimeMS();
				state=CCSTATE_NORMAL;
			}
			else if(!msg.empty())
			{
				mempipe->Write("exit");
				mempipe=Server->createMemoryPipe();
				mempipe_owner=true;
				tcpstack.Send(pipe, msg);
				lasttime=Server->getTimeMS();
				state=CCSTATE_NORMAL;
				IScopedLock lock(backup_mutex);
				removeRunningProcess(local_backup_running_id, false);
			}
			else if( ( file_version>1 || state==CCSTATE_START_FILEBACKUP_ASYNC)
				&& Server->getTimeMS()-last_update_time>30000 )
			{
				last_update_time=Server->getTimeMS();
				tcpstack.Send(pipe, "BUSY");
			}
		}break;
	case CCSTATE_SHADOWCOPY:
		{
			std::string msg;
			mempipe->Read(&msg, 0);
			if (!msg.empty())
			{
				mempipe->Write("exit");
				mempipe = Server->createMemoryPipe();
				mempipe_owner = true;
				if (msg == "exit")
				{
					if (waitForThread())
					{
						do_quit = true;
						return true;
					}
					return false;
				}
				else if (msg.find("done") == 0)
				{
					tcpstack.Send(pipe, "DONE");
				}
				else if (msg.find("failed") == 0)
				{
					tcpstack.Send(pipe, "FAILED");
				}
				else if (msg.find("in use") == 0)
				{
					tcpstack.Send(pipe, "IN USE");
				}
				else
				{
					Server->Log("Unknown msg " + msg + " in CCSTATE_SHADOWCOPY", LL_ERROR);
					assert(false);
				}
				lasttime = Server->getTimeMS();
				state = CCSTATE_NORMAL;
			}
		}break;
	case CCSTATE_CHANNEL: //Channel
		{		
			IScopedLock lock(backup_mutex);

			SChannel* chan = getCurrChannel();

			bool has_ping = chan != NULL && chan->state == SChannel::EChannelState_Pinging;

			int64 timeout_interval = 180000;

			int64 ctime = Server->getTimeMS();
			if(ctime-lasttime>timeout_interval
				|| (has_ping && ctime-last_channel_ping>10000) )
			{
				std::string extra;
				if (has_ping && ctime - last_channel_ping > 10000)
				{
					extra = " (ping timeout)";
				}
				else
				{
					extra = " (total timeout)";
				}
				Server->Log("Client timeout in ClientConnector::Run - Channel"+extra, LL_DEBUG);
				for(size_t i=0;i<channel_pipes.size();++i)
				{
					if(channel_pipes[i].pipe==pipe)
					{
						channel_pipes.erase(channel_pipes.begin()+i);
						break;
					}
				}
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				return false;
			}
			if(chan!=NULL && chan->state==SChannel::EChannelState_Exit)
			{
				do_quit=true;
				break;
			}
			if(Server->getTimeMS()-last_channel_ping>60000
				&& chan->state==SChannel::EChannelState_Idle)
			{
				tcpstack.Send(pipe, "PING");
				last_channel_ping=Server->getTimeMS();
				chan->state = SChannel::EChannelState_Pinging;
			}
			if(make_fileserv
				&& chan->state == SChannel::EChannelState_Idle)
			{
				size_t idx=std::string::npos;
				for(size_t i=0;i<channel_pipes.size();++i)
				{
					if(channel_pipes[i].pipe==pipe)
					{
						idx=i;
						break;
					}
				}

				if(idx!=std::string::npos)
				{
					tcpstack.Send(pipe, "FILESERV");
					state=CCSTATE_FILESERV;
					fileserv_connections.push_back(SFilesrvConnection(channel_pipes[idx].token, pipe));

					channel_pipes.erase(channel_pipes.begin()+idx);
				}				

				return false;
			}
		}break;
	case CCSTATE_IMAGE:
	case CCSTATE_IMAGE_HASHDATA:
	case CCSTATE_IMAGE_BITMAP:
		{
			if(Server->getThreadPool()->isRunning(image_inf.thread_ticket)==false )
			{
				delete image_inf.image_thread;
				image_inf.image_thread=NULL;
				return false;
			}
		}break;
	case CCSTATE_UPDATE_DATA:
	case CCSTATE_UPDATE_FINISH:
		{
			if(Server->getTimeMS()-lasttime>10000)
			{
				Server->Log("Client timeout in ClientConnector::Run-update(state6|7)", LL_DEBUG);
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				return false;
			}

			if(state==CCSTATE_UPDATE_FINISH)
			{
				if(hashdataok)
				{
					hashdatafile->Seek(0);
					writeUpdateFile(hashdatafile, UPDATE_FILE_PREFIX "version_new.txt");
					writeUpdateFile(hashdatafile, UPDATE_FILE_PREFIX "UrBackupUpdate.sig2");
					writeUpdateFile(hashdatafile, UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat");

					std::string hashdatafile_fn=hashdatafile->getFilename();
					Server->destroy(hashdatafile);
					Server->deleteFile(hashdatafile_fn);

					if(crypto_fak!=NULL)
					{
						IFile *updatefile=Server->openFile(UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat");
						if(updatefile!=NULL)
						{
							if(checkHash(getSha512Hash(updatefile)))
							{
								Server->destroy(updatefile);
								if(crypto_fak->verifyFile(UPDATE_SIGNATURE_PREFIX "urbackup_ecdsa409k1.pub",
									UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat", UPDATE_FILE_PREFIX "UrBackupUpdate.sig2"))
								{
#ifdef _WIN32
									Server->deleteFile(UPDATE_FILE_PREFIX "UrBackupUpdate.exe");
									moveFile(UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat", UPDATE_FILE_PREFIX "UrBackupUpdate.exe");
#else
									Server->deleteFile(UPDATE_FILE_PREFIX "UrBackupUpdate.sh");
									moveFile(UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat", UPDATE_FILE_PREFIX "UrBackupUpdate.sh");
#endif
									
									tcpstack.Send(pipe, "ok");

									if(silent_update)
									{
										update_silent();
									}
									else
									{
										Server->deleteFile(UPDATE_FILE_PREFIX "version.txt");
										moveFile(UPDATE_FILE_PREFIX "version_new.txt", UPDATE_FILE_PREFIX "version.txt");
									}
								}
								else
								{									
									Server->Log("Verifying update file failed. Signature did not match", LL_ERROR);
									Server->deleteFile(UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat");
									tcpstack.Send(pipe, "verify_sig_err");
								}
							}
							else
							{
								Server->destroy(updatefile);
								Server->Log("Verifying update file failed. Update was installed previously", LL_ERROR);
								Server->deleteFile(UPDATE_FILE_PREFIX "UrBackupUpdate_untested.dat");
								tcpstack.Send(pipe, "verify_sig_already_used_err");
							}
						}
					}
					else
					{
						Server->Log("Verifying update file failed. Cryptomodule not present", LL_ERROR);
						tcpstack.Send(pipe, "verify_cryptmodule_err");
					}

					state=CCSTATE_NORMAL;
				}
				else
				{
					do_quit=true;
				}
			}
			return true;
		}break;
	case CCSTATE_WAIT_FOR_CONTRACTORS: // wait for contractors
		{
			for(size_t i=0;i<contractors.size();++i)
			{
				std::string resp;
				contractors[i]->Read(&resp, 0);
				if(!resp.empty())
				{
					contractors[i]->Write("exit");
					contractors.erase(contractors.begin()+i);
					break;
				}
			}

			if(contractors.empty())
			{
				return false;
			}
		}break;
	case CCSTATE_STATUS:
		{
			IScopedLock lock(backup_mutex);
			if(Server->getTimeMS()-lasttime>50000 || status_updated)
			{
				sendStatus();

				lasttime = Server->getTimeMS();
				state = CCSTATE_NORMAL;
				status_updated=false;
			}
			return true;
		} break;

	}
	return true;
}

std::string ClientConnector::getSha512Hash(IFile *fn)
{
	sha512_ctx ctx;
	char buf[4096];
	_u32 r;
	sha512_init(&ctx);
	while((r=fn->Read(buf, 4096))!=0)
	{
		sha512_update(&ctx, (unsigned char*)buf, r);
	}
	unsigned char digest[64];
	sha512_final(&ctx,digest);
	return bytesToHex(digest, 64);
}

bool ClientConnector::checkHash(std::string shah)
{
	std::string prev_h=getFile(UPDATE_FILE_PREFIX "updates_h.dat");
	std::vector<std::string> lines;
	TokenizeMail(prev_h, lines, "\n");
	for(size_t i=0;i<lines.size();++i)
	{
		std::string l=strlower(trim(lines[i]));
		if(!l.empty() && next(shah, 0, l))
		{
			return false;
		}
	}
	return true;
}

bool ClientConnector::writeUpdateFile(IFile *datafile, std::string outfn)
{
	unsigned int size;
	if(datafile->Read((char*)&size, sizeof(unsigned int))!=sizeof(unsigned int))
		return false;

	IFile *out=Server->openFile(outfn, MODE_WRITE);
	if(out==NULL)
		return false;

	size_t read=0;
	char buf[4096];
	while(true)
	{
		size_t tr=(std::min)((size_t)4096, size-read);
		if(tr==0)
			break;

		if(datafile->Read(buf, (_u32)tr)!=tr)
		{
			Server->destroy(out);
			return false;
		}

		if(out->Write(buf, (_u32)tr)!=tr)
		{
			Server->destroy(out);
			return false;
		}

		read+=tr;
	}

	Server->destroy(out);
	return true;
}

void ClientConnector::ReceivePackets(IRunOtherCallback* p_run_other)
{
	run_other = p_run_other;

	if(state==CCSTATE_WAIT_FOR_CONTRACTORS || state==CCSTATE_UPDATE_FINISH)
	{
		return;
	}

	IMutex *l_mutex=NULL;
	if(is_channel)
	{
		l_mutex=backup_mutex;
	}
	IScopedLock g_lock(l_mutex);

	std::string cmd;
	size_t rc=pipe->Read(&cmd, is_channel?0:-1);
	if(rc==0 )
	{
		if(!do_quit)
		{
			Server->Log("rc=0 hasError="+convert(pipe->hasError())+" state="+convert(state), LL_DEBUG);
#ifdef _WIN32
#ifdef _DEBUG
			Server->Log("Err: "+convert((int)GetLastError()), LL_DEBUG);
#endif
#endif
		}
		
		if(is_channel && pipe->hasError())
		{
			do_quit=true;
		}
		else if(!is_channel)
		{
			do_quit=true;
		}
		else
		{
			lasttime=Server->getTimeMS();
		}
		return;
	}

	while(state==CCSTATE_IMAGE_HASHDATA
		|| state==CCSTATE_UPDATE_DATA
		|| state== CCSTATE_IMAGE_BITMAP)
	{
		lasttime=Server->getTimeMS();

		IFile* datafile;
		unsigned int* dataleft;
		if (state == CCSTATE_IMAGE_HASHDATA
			|| state == CCSTATE_UPDATE_DATA)
		{
			datafile = hashdatafile;
			dataleft = &hashdataleft;
		}
		else
		{
			datafile = bitmapfile;
			dataleft = &bitmapleft;
		}

		unsigned int towrite = (std::min)(*dataleft, static_cast<unsigned int>(cmd.size()));

		if(datafile->Write(cmd.substr(0, towrite))!=towrite)
		{
			Server->Log("Error writing to data to temporary file", LL_ERROR);
			do_quit=true;
			return;
		}

		*dataleft -= towrite;

		if(*dataleft==0)
		{
			if (state == CCSTATE_IMAGE_HASHDATA
				|| state == CCSTATE_UPDATE_DATA)
			{
				if (bitmapfile == NULL)
				{
					hashdataok = true;
					if (state == CCSTATE_IMAGE_HASHDATA)
						state = CCSTATE_IMAGE;
					else if (state == CCSTATE_UPDATE_DATA)
						state = CCSTATE_UPDATE_FINISH;

					if (towrite != cmd.size())
					{
						Server->Log("Too much hash data (needed="+convert(towrite)+" got="+convert(cmd.size()), LL_ERROR);
					}

					return;
				}
				else
				{
					cmd.erase(0, towrite);
					state = CCSTATE_IMAGE_BITMAP;

					if (cmd.empty())
					{
						return;
					}
				}
			}
			else
			{
				if (towrite != cmd.size())
				{
					Server->Log("Too much bitmap data (needed=" + convert(towrite) + " got=" + convert(cmd.size()), LL_ERROR);
				}

				hashdataok = true;
				state = CCSTATE_IMAGE;

				return;
			}			
		}
		else
		{
			return;
		}
	}

	tcpstack.AddData((char*)cmd.c_str(), cmd.size());

	while( tcpstack.getPacket(cmd) && !cmd.empty())
	{
		Server->Log("ClientService cmd: "+cmd, LL_DEBUG);

		bool pw_ok=false;
		bool pw_change_ok=false;
		std::string identity;
		bool ident_ok=false;
		str_map params;
		size_t hashpos;
		if(cmd.size()>3 && cmd[0]=='#' && cmd[1]=='I' ) //From server
		{
			identity=getbetween("#I", "#", cmd);
			replaceNonAlphaNumeric(identity, '_');
			cmd.erase(0, identity.size()+3);
			size_t tp=cmd.find("#token=");
			if(tp!=std::string::npos)
			{
				server_token=cmd.substr(tp+7);
				cmd.erase(tp, cmd.size()-tp);
			}
		}
		else if((hashpos=cmd.find("#"))!=std::string::npos) //From front-end
		{
			ParseParamStrHttp(getafter("#", cmd), &params, false);

			cmd.erase(hashpos, cmd.size()-hashpos);
			if(!checkPassword(params["pw"], pw_change_ok))
			{
				Server->Log("Password wrong!", LL_ERROR);
                do_quit=true;
				continue;
			}
			else
			{
				pw_ok=true;
			}
		}

		if(!identity.empty() && ServerIdentityMgr::checkServerSessionIdentity(identity, endpoint_name))
		{
			ident_ok=true;
		}
		else if(!identity.empty() && ServerIdentityMgr::checkServerIdentity(identity))
		{
			if(!ServerIdentityMgr::hasPublicKey(identity) || crypto_fak==NULL)
			{
				ident_ok=true;
			}
		}

		if( (ident_ok || is_channel) && !internet_conn )
		{
			InternetClient::hasLANConnection();
		}

		if(cmd=="ADD IDENTITY" )
		{
			CMD_ADD_IDENTITY(identity, cmd, ident_ok); continue;
		}
		else if(next(cmd, 0, "GET CHALLENGE"))
		{
			CMD_GET_CHALLENGE(identity, cmd); continue;
		}
		else if(next(cmd, 0, "SIGNATURE"))
		{
			CMD_SIGNATURE(identity, cmd); continue;
		}
		else if(next(cmd, 0, "FULL IMAGE ") )
		{
			CMD_FULL_IMAGE(cmd, ident_ok); continue;
		}
		else if(next(cmd, 0, "INCR IMAGE ") )
		{
			CMD_INCR_IMAGE(cmd, ident_ok); continue;
		}

		if(ident_ok) //Commands from Server
		{
			if( cmd=="START BACKUP" || cmd=="2START BACKUP" || next(cmd, 0, "3START BACKUP") )
			{
				CMD_START_INCR_FILEBACKUP(cmd); continue;
			}
			else if( cmd=="START FULL BACKUP" || cmd=="2START FULL BACKUP" || next(cmd, 0, "3START FULL BACKUP") )
			{
				CMD_START_FULL_FILEBACKUP(cmd); continue;
			}
			else if (next(cmd, 0, "WAIT FOR INDEX "))
			{
				CMD_WAIT_FOR_INDEX(cmd); continue;
			}
			else if(next(cmd, 0, "START SC \"") )
			{
				CMD_START_SHADOWCOPY(cmd); continue;
			}
			else if(next(cmd, 0, "STOP SC \"") )
			{
				CMD_STOP_SHADOWCOPY(cmd); continue;
			}
			else if(next(cmd, 0, "INCRINTERVALL \"") )
			{
				CMD_SET_INCRINTERVAL(cmd); continue;
			}
			else if(cmd=="DID BACKUP" )
			{
				CMD_DID_BACKUP(cmd); continue;
			}
			else if (next(cmd, 0, "2DID BACKUP "))
			{
				CMD_DID_BACKUP2(cmd); continue;
			}
			else if(next(cmd, 0, "SETTINGS ") )
			{
				CMD_UPDATE_SETTINGS(cmd); continue;
			}
			else if(next(cmd, 0, "PING RUNNING") )
			{
				CMD_PING_RUNNING(cmd); continue;
			}
			else if(next(cmd, 0, "2PING RUNNING ") )
			{
				CMD_PING_RUNNING2(cmd); continue;
			}
			else if( next(cmd, 0, "1CHANNEL") )
			{
				CMD_CHANNEL(cmd, &g_lock, identity); continue;
			}
			else if(next(cmd, 0, "2LOGDATA ") )
			{
				CMD_LOGDATA(cmd); continue;
			}
			else if( next(cmd, 0, "MBR ") )
			{
				CMD_MBR(cmd); continue;
			}
			else if( next(cmd, 0, "VERSION ") )
			{
				CMD_VERSION_UPDATE(cmd); continue;
			}
			else if( next(cmd, 0, "1CLIENTUPDATE ") )
			{
				CMD_CLIENT_UPDATE(cmd); continue;
			}
			else if(next(cmd, 0, "CAPA") )
			{
				CMD_CAPA(cmd); continue;
			}
			else if( cmd=="ENABLE END TO END FILE BACKUP VERIFICATION")
			{
				CMD_ENABLE_END_TO_END_FILE_BACKUP_VERIFICATION(cmd); continue;
			}
			else if( cmd=="GET VSSLOG")
			{
				CMD_GET_VSSLOG(cmd); continue;
			}
			else if( cmd=="CONTINUOUS WATCH START")
			{
				CMD_CONTINUOUS_WATCH_START(); continue;
			}
			else if( next(cmd, 0, "SCRIPT STDERR ") )
			{
				CMD_SCRIPT_STDERR(cmd); continue;
			}
			else if( next(cmd, 0, "FILE RESTORE "))
			{
				CMD_FILE_RESTORE(cmd.substr(13)); continue;
			}
		}
		if(pw_ok) //Commands from client frontend
		{
			if(pw_change_ok) //Administrator commands
			{
				if(cmd=="SAVE BACKUP DIRS" )
				{
					CMD_SAVE_BACKUPDIRS(cmd, params); continue;
				}
				else if(next(cmd, 0, "UPDATE SETTINGS ") )
				{
					CMD_TOCHANNEL_UPDATE_SETTINGS(cmd); continue;
				}
				else if(cmd=="GET LOGPOINTS" )
				{
					CMD_GET_LOGPOINTS(cmd); continue;
				}
				else if(cmd=="GET LOGDATA" )
				{
					CMD_GET_LOGDATA(cmd, params); continue;
				}
				else if( cmd=="NEW SERVER" )
				{
					CMD_NEW_SERVER(params); continue;
				}
				else if (cmd == "RESET KEEP")
				{
					CMD_RESET_KEEP(params); continue;
				}
			}
			
			if( cmd=="GET BACKUP DIRS" )
			{
				CMD_GET_BACKUPDIRS(cmd); continue;
			}
			else if(cmd=="STATUS" || cmd=="FSTATUS" )
			{
				CMD_STATUS(cmd); continue;
			}
			else if(cmd=="STATUS DETAIL")
			{
				CMD_STATUS_DETAIL(cmd); continue;
			}
			else if(cmd=="START BACKUP INCR" )
			{
				CMD_TOCHANNEL_START_INCR_FILEBACKUP(cmd); continue;			
			}
			else if(cmd=="START BACKUP FULL" )
			{
				CMD_TOCHANNEL_START_FULL_FILEBACKUP(cmd); continue;
			}
			else if(cmd=="START IMAGE FULL" )
			{
				CMD_TOCHANNEL_START_FULL_IMAGEBACKUP(cmd); continue;
			}
			else if(cmd=="START IMAGE INCR" )
			{
				CMD_TOCHANNEL_START_INCR_IMAGEBACKUP(cmd); continue;
			}			
			else if(next(cmd, 0, "PAUSE ") )
			{
				CMD_PAUSE(cmd); continue;
			}			
			else if( cmd.find("GET BACKUPCLIENTS")==0 )
			{
				CMD_RESTORE_GET_BACKUPCLIENTS(cmd); continue;
			}
			else if( cmd.find("GET BACKUPIMAGES ")==0 )
			{
				CMD_RESTORE_GET_BACKUPIMAGES(cmd); continue;
			}
			else if( cmd=="GET FILE BACKUPS" )
			{
				CMD_RESTORE_GET_FILE_BACKUPS(cmd); continue;
			}
			else if( cmd=="GET FILE BACKUPS TOKENS")
			{
				CMD_RESTORE_GET_FILE_BACKUPS_TOKENS(cmd, params); continue;
			}
			else if(cmd=="GET FILE LIST TOKENS")
			{
				CMD_GET_FILE_LIST_TOKENS(cmd, params); continue;
			}
			else if(cmd=="DOWNLOAD FILES TOKENS")
			{
				CMD_DOWNLOAD_FILES_TOKENS(cmd, params); continue;
			}
			else if( cmd=="LOGIN FOR DOWNLOAD" )
			{
				CMD_RESTORE_LOGIN_FOR_DOWNLOAD(cmd, params); continue;
			}
			else if( cmd=="GET SALT" )
			{
				CMD_RESTORE_GET_SALT(cmd, params); continue;
			}
			else if( cmd=="DOWNLOAD IMAGE" )
			{
				CMD_RESTORE_DOWNLOAD_IMAGE(cmd, params); continue;
			}		
			else if( cmd=="DOWNLOAD FILES" )
			{
				CMD_RESTORE_DOWNLOAD_FILES(cmd, params); continue;
			}	
			else if( cmd=="GET DOWNLOADPROGRESS" )
			{
				CMD_RESTORE_DOWNLOADPROGRESS(cmd); continue;		
			}
			else if( cmd=="GET ACCESS PARAMETERS")
			{
				CMD_GET_ACCESS_PARAMS(params); continue;
			}
			else if( cmd=="RESTORE OK")
				{
					CMD_RESTORE_OK(params); continue;
				}
		}
		if(is_channel) //Channel commands from server
		{
			if(cmd=="PONG" )
			{
				CMD_CHANNEL_PONG(cmd, endpoint_name); continue;
			}
			else if(cmd=="PING" )
			{
				CMD_CHANNEL_PING(cmd, endpoint_name); continue;
			}
		}
		
		tcpstack.Send(pipe, "ERR");
	}
}

bool ClientConnector::checkPassword(const std::string &pw, bool& change_pw)
{
	static std::string stored_pw=getFile(pw_file);
	static std::string stored_pw_change=getFile(pw_change_file);
	std::string utf8_pw = (pw);
	if(stored_pw_change==utf8_pw)
	{
		change_pw=true;
		return true;
	}
	if(stored_pw==utf8_pw)
	{
		change_pw=false;
		return true;
	}
	return false;
}

namespace
{
	std::string removeChars(std::string in)
	{
		char illegalchars[] = {'*', ':', '/' , '\\'};
		std::string ret;
		for(size_t i=0;i<in.size();++i)
		{
			bool found=false;
			for(size_t j=0;j<sizeof(illegalchars)/sizeof(illegalchars[0]);++j)
			{
				if(illegalchars[j]==in[i])
				{
					found=true;
					break;
				}
			}
			if(!found)
			{
				ret+=in[i];
			}
		}
		return ret;
	}
}


bool ClientConnector::saveBackupDirs(str_map &args, bool server_default, int group_offset)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	db->BeginWriteTransaction();
	db_results backupdirs=db->Prepare("SELECT name, path FROM backupdirs")->Read();
	if (args.find("all_virtual_clients") != args.end())
	{
		db->Write("DELETE FROM backupdirs WHERE symlinked=0");
	}
	else
	{
		db->Write("DELETE FROM backupdirs WHERE symlinked=0 AND tgroup BETWEEN " + convert(group_offset) + " AND " + convert(group_offset + c_group_max));
	}
	IQuery *q=db->Prepare("INSERT INTO backupdirs (name, path, server_default, optional, tgroup) VALUES (?, ? ,"+convert(server_default?1:0)+", ?, ?)");
	/**
	Use empty client settings
	if(server_default==false)
	{
		q->Bind(L"*"); q->Bind(L"*"); q->Bind(1);
		q->Write();
		q->Reset();
	}
	*/
	IQuery *q2=db->Prepare("SELECT id FROM backupdirs WHERE name=?");
	IQuery* q_get_virtual_client_offset = db->Prepare("SELECT group_offset FROM virtual_client_group_offsets WHERE virtual_client=?");
	std::string dir;
	size_t i=0;
	std::vector<SBackupDir> new_watchdirs;
	std::vector<std::string> all_backupdirs;
	do
	{
		dir=args["dir_"+convert(i)];
		if(!dir.empty())
		{
			all_backupdirs.push_back(dir);

			std::string name;
			str_map::iterator name_arg=args.find("dir_"+convert(i)+"_name");
			if(name_arg!=args.end() && !name_arg->second.empty())
				name=name_arg->second;
			else
				name=ExtractFileName(dir);

			int curr_offset = group_offset;

			str_map::iterator virtual_client_arg = args.find("dir_" + convert(i) + "_virtual_client");
			if (virtual_client_arg != args.end() && !virtual_client_arg->second.empty())
			{
				q_get_virtual_client_offset->Bind(virtual_client_arg->second);
				db_results res_offset = q_get_virtual_client_offset->Read();
				q_get_virtual_client_offset->Reset();
				if (!res_offset.empty())
				{
					curr_offset = watoi(res_offset[0]["group_offset"]);
				}
			}

			int group = curr_offset + c_group_default;

			str_map::iterator group_arg=args.find("dir_"+convert(i)+"_group");
			if(group_arg!=args.end() && !group_arg->second.empty())
				group= curr_offset + watoi(group_arg->second);

			int flags = EBackupDirFlag_FollowSymlinks | EBackupDirFlag_SymlinksOptional | EBackupDirFlag_ShareHashes; //default flags
			size_t flags_off = name.find("/");
			if(flags_off!=std::string::npos)
			{
				flags=0;

				std::vector<std::string> str_flags;
				TokenizeMail(getafter("/", name), str_flags, ",;");

				for(size_t i=0;i<str_flags.size();++i)
				{
					std::string flag = strlower(trim(str_flags[i]));
					if(flag=="optional")
					{
						flags |= EBackupDirFlag_Optional;
					}
					else if(flag=="follow_symlinks")
					{
						flags |= EBackupDirFlag_FollowSymlinks;
					}
					else if(flag=="symlinks_optional")
					{
						flags |= EBackupDirFlag_SymlinksOptional;
					}
					else if(flag=="one_filesystem" || flag=="one_fs")
					{
						flags |= EBackupDirFlag_OneFilesystem;
					}
					else if (flag == "require_snapshot" || flag=="require_shadowcopy" )
					{
						flags |= EBackupDirFlag_RequireSnapshot;
					}
					else if (flag == "share_hashes")
					{
						flags |= EBackupDirFlag_ShareHashes;
					}
					else if (flag == "keep" || flag == "keep_files")
					{
						flags |= EBackupDirFlag_KeepFiles;
					}
				}
				name.resize(flags_off);
			}

			name=removeChars(name);

			if(dir[dir.size()-1]=='\\' || dir[dir.size()-1]=='/' )
			{
				dir.erase(dir.size()-1,1);
#ifndef _WIN32
				if(dir.empty())
				{
					dir="/";
					if(name.empty())
					{
						name="root";
					}
				}
#endif
			}

			q2->Bind(name);
			if(q2->Read().empty()==false)
			{
				for(int k=0;k<100;++k)
				{
					q2->Reset();
					q2->Bind(name+"_"+convert(k));
					if(q2->Read().empty()==true)
					{
						name+="_"+convert(k);
						break;
					}
				}
			}
			q2->Reset();

			bool found=false;
			for(size_t i=0;i<backupdirs.size();++i)
			{
				if(backupdirs[i]["path"]==dir)
				{
					backupdirs[i]["need"]="1";
					backupdirs[i]["group"] = convert(group);
					found=true;
					break;
				}
			}

			if(!found)
			{
				//It's not already watched. Add it
				SBackupDir new_dir = {
					0, name, dir, flags, group };

				new_watchdirs.push_back(new_dir);
			}
			
			q->Bind(name);
			q->Bind(dir);
			q->Bind(flags);
			q->Bind(group);
			q->Write();
			q->Reset();
		}
		++i;
	}
	while(!dir.empty());
	db->EndTransaction();

#ifdef _WIN32
	for(size_t i=0;i<new_watchdirs.size();++i)
	{
		//Add watch
		IPipe *contractor=Server->createMemoryPipe();
		CWData data;
		data.addChar(IndexThread::IndexThreadAction_AddWatchdir);
		data.addVoidPtr(contractor);
		data.addString((new_watchdirs[i].path));
		if(new_watchdirs[i].group==c_group_continuous)
		{
			data.addString((new_watchdirs[i].tname));
		}
		IndexThread::stopIndex();
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
		contractors.push_back(contractor);
		state=CCSTATE_WAIT_FOR_CONTRACTORS;
		want_receive=false;
	}

	for(size_t i=0;i<backupdirs.size();++i)
	{
		if(backupdirs[i]["need"]!="1" && backupdirs[i]["path"]!="*")
		{
			//Delete the watch
			IPipe *contractor=Server->createMemoryPipe();
			CWData data;
			data.addChar(IndexThread::IndexThreadAction_RemoveWatchdir);
			data.addVoidPtr(contractor);
			data.addString((backupdirs[i]["path"]));
			if(watoi(backupdirs[i]["group"])%c_group_size==c_group_continuous)
			{
				data.addString((backupdirs[i]["name"]));
			}
			IndexThread::stopIndex();
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			contractors.push_back(contractor);
			state=CCSTATE_WAIT_FOR_CONTRACTORS;
			want_receive=false;
		}
	}

	if (!new_watchdirs.empty())
	{
		CWData data;
		data.addChar(IndexThread::IndexThreadAction_UpdateCbt);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}

	if(Server->fileExists(Server->getServerWorkingDir()+"\\UrBackupClient.exe"))
	{
		{
			bool deleted_key;
			size_t i=0;
			do 
			{
				deleted_key=false;
				if(RegDeleteKeyW(HKEY_CLASSES_ROOT, Server->ConvertToWchar("AllFilesystemObjects\\shell\\urbackup.access." + convert(i)+"\\Command").c_str())==ERROR_SUCCESS
					&& RegDeleteKeyW(HKEY_CLASSES_ROOT, Server->ConvertToWchar("AllFilesystemObjects\\shell\\urbackup.access."+convert(i)).c_str())==ERROR_SUCCESS)
				{
					deleted_key=true;
				}
				++i;
			} while (deleted_key);
		}


		std::wstring mui_text=L"&Access backups";
		std::string read_mui_text=getFile("access_backups_shell_mui.txt");
		if(	!read_mui_text.empty() && read_mui_text.find("@")==std::string::npos)
		{
			mui_text=Server->ConvertToWchar(read_mui_text);
		}

		/**
		* The Advanced Query Syntax (AQL) in appliesTo seems to have a length restriction. 
		* That's why each entry for each folder is added separately instead of using "OR".
		*/
		for(size_t i=0;i<all_backupdirs.size();++i)
		{
			HKEY urbackup_access;
			if(RegCreateKeyExW(HKEY_CLASSES_ROOT, Server->ConvertToWchar("AllFilesystemObjects\\shell\\urbackup.access."+convert(i)).c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &urbackup_access, NULL)==ERROR_SUCCESS)
			{
				HKEY urbackup_access_command;
				if(RegCreateKeyExW(HKEY_CLASSES_ROOT, Server->ConvertToWchar("AllFilesystemObjects\\shell\\urbackup.access."+convert(i)+"\\Command").c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &urbackup_access_command, NULL)!=ERROR_SUCCESS)
				{
					Server->Log("Error creating command registry sub-key", LL_ERROR);
				}
				else
				{
					std::wstring cmd=Server->ConvertToWchar("\""+Server->getServerWorkingDir()+"\\UrBackupClient.exe\" access \"%1\"");
					//cmd = greplace(L"\\", L"\\\\", cmd);
					if(RegSetValueExW(urbackup_access_command, NULL, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), static_cast<DWORD>(cmd.size()*sizeof(wchar_t)))!=ERROR_SUCCESS)
					{
						Server->Log("Error setting command in registry", LL_ERROR);
					}
				}

				if(RegSetValueExW(urbackup_access, L"MUIVerb", 0, REG_SZ, reinterpret_cast<const BYTE*>(mui_text.c_str()), static_cast<DWORD>(mui_text.size()*sizeof(wchar_t)))!=ERROR_SUCCESS)
				{
					Server->Log("Error setting MUIVerb in registry", LL_ERROR);
				}

				std::wstring icon_path = Server->ConvertToWchar(Server->getServerWorkingDir()+"\\backup-ok.ico");

				if(RegSetValueExW(urbackup_access, L"Icon", 0, REG_SZ, reinterpret_cast<const BYTE*>(icon_path.c_str()), static_cast<DWORD>(icon_path.size()*sizeof(wchar_t)))!=ERROR_SUCCESS)
				{
					Server->Log("Error setting Icon in registry", LL_ERROR);
				}			

				std::string path = greplace("/", "\\", all_backupdirs[i]);

				std::wstring applies_to=Server->ConvertToWchar("System.ParsingPath:~<\""+path+"\"");

				if(RegSetValueExW(urbackup_access, L"AppliesTo", 0, REG_SZ, reinterpret_cast<const BYTE*>(applies_to.c_str()), static_cast<DWORD>(applies_to.size()*sizeof(wchar_t)))!=ERROR_SUCCESS)
				{
					Server->Log("Error setting AppliesTo in registry", LL_ERROR);
				}
			}
		}
	}	
#endif
	db->destroyAllQueries();
	return true;
}

std::string ClientConnector::replaceChars(std::string in)
{
	char legalchars[] = {'_', '-'};
	for(size_t i=0;i<in.size();++i)
	{
		bool found=false;
		for(size_t j=0;j<sizeof(legalchars);++j)
		{
			if(legalchars[j]==in[i])
			{
				found=true;
				break;
			}
		}
		if( !isletter(in[i]) && !str_isnumber(in[i]) && !found )
		{
			in[i]='_';
		}
	}
	return in;
}

void ClientConnector::updateLastBackup(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("UPDATE status SET last_backup=CURRENT_TIMESTAMP WHERE id=1");
	IQuery *q2=db->Prepare("SELECT last_backup FROM status WHERE id=1");
	if(q!=NULL && q2!=NULL)
	{
		if(q2->Read().size()>0)
		{
			q->Write();
		}
		else
		{
			q=db->Prepare("INSERT INTO status (last_backup, id) VALUES (CURRENT_TIMESTAMP, 1)");
			if(q!=NULL)
				q->Write();
		}
	}
	db->destroyAllQueries();
}

std::vector<std::string> getSettingsList(void);

void ClientConnector::updateSettings(const std::string &pData)
{
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	std::auto_ptr<ISettingsReader> new_settings(Server->createMemorySettingsReader(pData));

	std::string settings_fn="urbackup/data/settings.cfg";
	std::string settings_server_fn="urbackup/data/settings_"+server_token+".cfg";
	std::string clientsubname;
	std::string str_group_offset;
	int group_offset=0;
	if(new_settings->getValue("clientsubname", &clientsubname) && !clientsubname.empty()
		&& new_settings->getValue("filebackup_group_offset", &str_group_offset))
	{
		settings_fn = "urbackup/data/settings_"+conv_filename(clientsubname)+".cfg";
		settings_server_fn = "urbackup/data/settings_"+conv_filename(clientsubname) + "_"+server_token+".cfg";
		group_offset = atoi(str_group_offset.c_str());

		db_results res_old_client = db->Read("SELECT virtual_client FROM virtual_client_group_offsets WHERE group_offset=" + convert(group_offset));
		if (!res_old_client.empty()
			&& res_old_client[0]["virtual_client"]!=clientsubname)
		{
			db->Write("DELETE FROM backupdirs WHERE tgroup=" + convert(group_offset));
			db->Write("DELETE FROM virtual_client_group_offsets WHERE group_offset=" + convert(group_offset));
		}
		IQuery* q = db->Prepare("INSERT OR REPLACE INTO virtual_client_group_offsets (virtual_client, group_offset) VALUES (?,?)", false);
		q->Bind(clientsubname);
		q->Bind(group_offset);
		q->Write();
		q->Reset();
		db->destroyQuery(q);
	}

	std::auto_ptr<ISettingsReader> curr_settings(Server->createFileSettingsReader(settings_fn));

	std::vector<std::string> settings_names=getSettingsList();
	settings_names.push_back("client_set_settings");
	settings_names.push_back("client_set_settings_time");
	std::string new_settings_str="";
	bool mod=false;
	std::string tmp_str;
	bool client_set_settings=false;
	if(curr_settings->getValue("client_set_settings", &tmp_str) && tmp_str=="true")
	{
		if( !new_settings->getValue("allow_overwrite", &tmp_str) )
			new_settings->getValue("allow_overwrite_def", &tmp_str);

		if( tmp_str!="false" )
		{
			client_set_settings=true;
		}
	}

	for(size_t i=0;i<settings_names.size();++i)
	{
		std::string key=settings_names[i];

		std::string v;
		std::string def_v;
		curr_settings->getValue(key+"_def", def_v);

		if(!curr_settings->getValue(key, &v) )
		{
			std::string nv;
			std::string new_key;
			if(new_settings->getValue(key, &nv) )
			{
				new_settings_str+=key+"="+nv+"\n";
				mod=true;
			}
			if(new_settings->getValue(key+"_def", &nv) )
			{
				new_settings_str+=key+"_def="+nv+"\n";
				if(nv!=def_v)
				{
					mod=true;
				}
			}	
		}
		else
		{
			if(client_set_settings)
			{
				std::string orig_v;
				std::string nv;
				if(new_settings->getValue(key+"_orig", &orig_v) &&
					orig_v==v &&
					(new_settings->getValue(key, &nv) ||
					 new_settings->getValue(key+"_def", &nv) ) )
				{
					new_settings_str+=key+"="+nv+"\n";
					if(nv!=v)
					{
						mod=true;
					}
				}
				else
				{
					new_settings_str+=key+"="+v+"\n";
				}
			}
			else
			{
				std::string nv;
				if(new_settings->getValue(key, &nv) )
				{
					if( (key=="internet_server" || key=="internet_server_def") && nv.empty() && !v.empty()
						|| (key=="computername" || key=="computername_def" ) && nv.empty() && !v.empty())
					{
						new_settings_str+=key+"="+v+"\n";
					}
					else
					{
						new_settings_str+=key+"="+nv+"\n";
						if(v!=nv)
						{
							mod=true;
						}
					}
				}
				else if( (key == "internet_server" || key == "internet_server_def") && !v.empty()
					|| (key == "computername" || key == "computername_def") && !v.empty())
				{
					new_settings_str+=key+"="+v+"\n";
				}

				if(new_settings->getValue(key+"_def", &nv) )
				{
					new_settings_str+=key+"_def="+nv+"\n";
					if(nv!=def_v)
					{
						mod=true;
					}
				}
			}
		}
	}

	IQuery *q=db->Prepare("SELECT id FROM backupdirs WHERE server_default=0 AND tgroup=?", false);
	q->Bind(group_offset);
	db_results res=q->Read();
	db->destroyQuery(q);
	if(res.empty())
	{
		std::string default_dirs;
		if(!new_settings->getValue("default_dirs", &default_dirs) )
			new_settings->getValue("default_dirs_def", &default_dirs);

		if(!default_dirs.empty())
		{
			std::vector<std::string> def_dirs_toks;
			Tokenize(default_dirs, def_dirs_toks, ";");
			str_map args;
			for(size_t i=0;i<def_dirs_toks.size();++i)
			{
				std::string path=trim(def_dirs_toks[i]);
				std::string name;
				int group = c_group_default;
				if(path.find("|")!=std::string::npos)
				{
					std::vector<std::string> toks;
					Tokenize(path, toks, "|");
					path = toks[0];
					name = toks[1];
					if(toks.size()>2)
					{
						group = (std::min)(c_group_max, (std::max)(0, watoi(toks[2])));
					}
				}
				args["dir_"+convert(i)]=path;
				if(!name.empty())
					args["dir_"+convert(i)+"_name"]=name;

				args["dir_"+convert(i)+"_group"]=convert(group);
			}

			saveBackupDirs(args, true, group_offset);
		}
	}

	if(mod)
	{
		writestring((new_settings_str), settings_fn);

		InternetClient::updateSettings();

		CWData data;
		data.addChar(IndexThread::IndexThreadAction_UpdateCbt);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}

	std::auto_ptr<ISettingsReader> curr_server_settings(Server->createFileSettingsReader(settings_server_fn));
	std::vector<std::string> global_settings = getGlobalizedSettingsList();

	std::string new_token_settings="";

	bool mod_server_settings=false;
	for(size_t i=0;i<global_settings.size();++i)
	{
		std::string key=global_settings[i];

		std::string v;
		bool curr_v=curr_server_settings->getValue(key, &v);
		std::string nv;
		bool new_v=new_settings->getValue(key, &nv);

		if(!curr_v && new_v)
		{
			new_token_settings+=key+"="+nv;
			mod_server_settings=true;
		}
		else if(curr_v)
		{
			if(new_v)
			{
				new_token_settings+=key+"="+nv;

				if(nv!=v)
				{
					mod_server_settings=true;
				}
			}
			else
			{
				new_token_settings+=key+"="+v;
			}
		}
	}

	if(mod_server_settings)
	{
		writestring((new_settings_str), settings_server_fn);
	}
}

void ClientConnector::replaceSettings(const std::string &pData)
{
	std::auto_ptr<ISettingsReader> new_settings(Server->createMemorySettingsReader(pData));

	std::string ncname=new_settings->getValue("computername", "");
	if(!ncname.empty() && ncname!=IndexThread::getFileSrv()->getServerName())
	{
		Server->Log("Restarting filesrv because of name change. Old name: " + IndexThread::getFileSrv()->getServerName() + " New name: " + ncname, LL_DEBUG);

		CWData data;
		data.addChar(7);
		data.addVoidPtr(NULL);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}

	bool keep_old_settings = (new_settings->getValue("keep_old_settings", "")=="true");

	std::string settings_fn="urbackup/data/settings.cfg";
	std::string clientsubname;
	if(new_settings->getValue("clientsubname", &clientsubname) && !clientsubname.empty())
	{
		settings_fn = "urbackup/data/settings_"+conv_filename(clientsubname)+".cfg";
	}

	std::auto_ptr<ISettingsReader> old_settings(Server->createFileSettingsReader(settings_fn));

	std::vector<std::string> new_keys = new_settings->getKeys();
	bool modified_settings=true;
	if(old_settings.get()!=NULL)
	{
		modified_settings=false;
		std::vector<std::string> old_keys = old_settings->getKeys();

		for(size_t i=0;i<old_keys.size();++i)
		{
			std::string old_val;
			std::string new_val;
			if( old_settings->getValue(old_keys[i], &old_val) &&
			    (!new_settings->getValue(old_keys[i], &new_val) ||
					old_val!=new_val ) )
			{
				modified_settings=true;
				break;
			}
		}

		if(!modified_settings)
		{
			for(size_t i=0;i<new_keys.size();++i)
			{
				std::string old_val;
				if(!old_settings->getValue(new_keys[i], &old_val))
				{
					modified_settings=true;
					break;
				}
			}
		}
	}

	if(modified_settings)
	{
		std::string new_data;

		for(size_t i=0;i<new_keys.size();++i)
		{
			if(new_keys[i]=="client_set_settings" ||
				new_keys[i]=="client_set_settings_time" ||
				new_keys[i]=="keep_old_settings" )
				continue;

			std::string val;
			if(new_settings->getValue(new_keys[i], &val))
			{
				new_data+=new_keys[i]+"="+val+"\n";
			}
		}

		if (keep_old_settings && old_settings.get() != NULL)
		{
			std::vector<std::string> old_keys = old_settings->getKeys();

			for (size_t i = 0; i < old_keys.size(); ++i)
			{
				if (std::find(new_keys.begin(), new_keys.end(), old_keys[i]) == new_keys.end())
				{
					std::string val;
					if (old_settings->getValue(old_keys[i], &val))
					{
						new_data += old_keys[i] + "=" + val + "\n";
					}					
				}
			}
		}

		InternetClient::updateSettings();

		new_data+="client_set_settings=true\n";
		new_data+="client_set_settings_time="+convert(Server->getTimeSeconds())+"\n";

		writestring(new_data, settings_fn);

		CWData data;
		data.addChar(IndexThread::IndexThreadAction_UpdateCbt);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}
}

void ClientConnector::saveLogdata(const std::string &created, const std::string &pData)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q_p=db->Prepare("INSERT INTO logs (ttime) VALUES (datetime(?, 'unixepoch'))");
	q_p->Bind(created);
	q_p->Write();
	_i64 logid=db->getLastInsertID();

	while(!db->BeginWriteTransaction())
				Server->wait(500);

	IQuery *q=db->Prepare("INSERT INTO logdata (logid, loglevel, message, idx, ltime) VALUES (?, ?, ?, ?, datetime(?, 'unixepoch'))");

	std::vector<std::string> lines;
	TokenizeMail(pData, lines, "\n");
	for(size_t i=0,lc=lines.size();i<lc;++i)
	{
		std::string l=lines[i];
		int loglevel=atoi(getuntil("-",l).c_str());
		std::string u_msg=getafter("-", l);
		unsigned int ltime=0;
		if(u_msg.find("-")!=std::string::npos)
		{
			std::string s_ltime=getuntil("-", u_msg);
			bool isnum=true;
			for(size_t j=0;j<s_ltime.size();++j)
			{
				if(!str_isnumber(s_ltime[j]))
				{
					isnum=false;
					break;
				}
			}
			if(isnum)
			{
				ltime=atoi(s_ltime.c_str());
				u_msg=getafter("-", u_msg);
			}
		}
		std::string msg=(u_msg);

		q->Bind(logid);
		q->Bind(loglevel);
		q->Bind(msg);
		q->Bind(i);
		q->Bind(ltime);
		q->Write();
		q->Reset();
	}
	db->EndTransaction();
	db->destroyAllQueries();
}

std::string ClientConnector::getLogpoints(void)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	int timeoutms=300;
	IQuery *q=db->Prepare("SELECT id, strftime('%s',ttime) AS ltime FROM logs ORDER BY ttime DESC LIMIT 100");
	db_results res=q->Read(&timeoutms);
	std::string ret;
	for(size_t i=0;i<res.size();++i)
	{
		ret+=(res[i]["id"])+"-";
		ret+=(res[i]["ltime"])+"\n";
	}
	db->destroyAllQueries();
	return ret;
}

void ClientConnector::getLogLevel(int logid, int loglevel, std::string &data)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT loglevel, message FROM logdata WHERE logid=? AND loglevel>=? ORDER BY idx ASC");
	q->Bind(logid);
	q->Bind(loglevel);
	int timeoutms=300;
	db_results res=q->Read(&timeoutms);
	for(size_t i=0;i<res.size();++i)
	{
		data+=(res[i]["loglevel"])+"-";
		data+=(res[i]["message"])+"\n";
	}
	db->destroyAllQueries();
}

bool ClientConnector::sendFullImage(void)
{
	image_inf.thread_action=TA_FULL_IMAGE;
	image_inf.image_thread=new ImageThread(this, pipe, mempipe, &image_inf, server_token, hashdatafile, NULL);
	mempipe=Server->createMemoryPipe();
	mempipe_owner=true;

	IScopedLock lock(backup_mutex);

	SRunningProcess* cproc = getRunningBackupProcess(server_token, image_inf.server_status_id);
	if (cproc != NULL
		&& cproc->action == RUNNING_FULL_IMAGE)
	{
		local_backup_running_id = cproc->id;
		cproc->last_pingtime = Server->getTimeMS();
	}
	else
	{
		SRunningProcess new_proc;
		new_proc.action = RUNNING_FULL_IMAGE;
		new_proc.server_token = server_token;
		new_proc.pcdone = 0;
		new_proc.details = image_inf.orig_image_letter;
		new_proc.server_id = image_inf.server_status_id;
		local_backup_running_id = addNewProcess(new_proc);
	}

	removeTimedOutProcesses(server_token, false);

	status_updated = true;
	image_inf.running_process_id = local_backup_running_id;
	image_inf.thread_ticket=Server->getThreadPool()->execute(image_inf.image_thread, "full image upload");
	state=CCSTATE_IMAGE;
	
	return true;
}

bool ClientConnector::sendIncrImage(void)
{
	image_inf.thread_action=TA_INCR_IMAGE;
	image_inf.image_thread=new ImageThread(this, pipe, mempipe, &image_inf, server_token, hashdatafile, bitmapfile);
	mempipe=Server->createMemoryPipe();
	mempipe_owner=true;

	IScopedLock lock(backup_mutex);

	SRunningProcess* cproc = getRunningBackupProcess(server_token, image_inf.server_status_id);
	if (cproc != NULL
		&& cproc->action == RUNNING_INCR_IMAGE )
	{
		local_backup_running_id = cproc->id;
		cproc->last_pingtime = Server->getTimeMS();
	}
	else
	{
		SRunningProcess new_proc;
		new_proc.action = RUNNING_INCR_IMAGE;
		new_proc.server_token = server_token;
		new_proc.pcdone = 0;
		new_proc.details = image_inf.orig_image_letter;
		new_proc.server_id = image_inf.server_status_id;

		local_backup_running_id = addNewProcess(new_proc);
	}

	removeTimedOutProcesses(server_token, false);

	status_updated = true;
	image_inf.running_process_id = local_backup_running_id;
	image_inf.thread_ticket=Server->getThreadPool()->execute(image_inf.image_thread, "incr image upload");
	
	return true;
}

bool ClientConnector::waitForThread(void)
{
	if(image_inf.thread_action!=TA_NONE && Server->getThreadPool()->isRunning(image_inf.thread_ticket ) )
		return true;
	else
		return false;
}

bool ClientConnector::sendMBR(std::string dl, std::string &errmsg)
{
#ifdef _WIN32
#pragma pack(push)
#pragma pack(1)
	struct EfiHeader
	{
		uint64 signature;
		_u32 revision;
		_u32 header_size;
		_u32 header_crc;
		_u32 reserved;
		int64 current_lba;
		int64 backup_lba;
		int64 first_lba;
		int64 last_lba;
		char disk_guid[16];
		int64 partition_table_lba;
		_u32 num_parition_entries;
		_u32 partition_entry_size;
		_u32 partition_table_crc;
	};
#pragma pack(pop)

	const uint64 gpt_magic = 0x5452415020494645ULL;

	std::string vpath=dl;
	if(!vpath.empty() && vpath[0]!='\\')
	{
		dl+=":";
		vpath="\\\\.\\"+dl;
	}

	HANDLE hVolume=CreateFileW(Server->ConvertToWchar(vpath).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		errmsg="CreateFile of volume '"+dl+"' failed. - sendMBR. Errorcode: "+convert((int)GetLastError());
		Server->Log(errmsg, LL_ERROR);
		return false;
	}

	bool gpt_style=false;
	unsigned int logical_sector_size=512;

	STORAGE_DEVICE_NUMBER dev_num;
	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(STORAGE_DEVICE_NUMBER), &ret_bytes, NULL);
	
	bool dynamic_volume=false;
	if(b==FALSE)
	{
		errmsg="DeviceIoControl IOCTL_STORAGE_GET_DEVICE_NUMBER failed. Volume: '"+dl+"'";
		Server->Log(errmsg, LL_WARNING);
		dynamic_volume=true;
	}

	{
		std::auto_ptr<VOLUME_DISK_EXTENTS> vde((VOLUME_DISK_EXTENTS*)new char[sizeof(VOLUME_DISK_EXTENTS)]);
		b=DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde.get(), sizeof(VOLUME_DISK_EXTENTS), &ret_bytes, NULL);
		if(b==0 && GetLastError()==ERROR_MORE_DATA)
		{
			DWORD ext_num=vde->NumberOfDiskExtents;
			errmsg="DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed. Extends: "+convert((int)ext_num);
			Server->Log(errmsg, LL_WARNING);
			DWORD vde_size=sizeof(VOLUME_DISK_EXTENTS)+sizeof(DISK_EXTENT)*(ext_num-1);
			vde.reset((VOLUME_DISK_EXTENTS*)new char[vde_size]);
			b=DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde.get(), vde_size, &ret_bytes, NULL);
			if(b==0)
			{
				errmsg="DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed twice. Volume: '"+dl+"'";
				Server->Log(errmsg, LL_ERROR);
				CloseHandle(hVolume);
				return false;
			}
		}
		else if(b==0)
		{
			errmsg="DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed. Volume: '"+dl+"' Error: "+convert((int)GetLastError());
			Server->Log(errmsg, LL_ERROR);
			CloseHandle(hVolume);
			return false;
		}

		if(vde->NumberOfDiskExtents>0)
		{
			HANDLE hDevice=CreateFileW(Server->ConvertToWchar("\\\\.\\PhysicalDrive"+convert((int)vde->Extents[0].DiskNumber)).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(hDevice==INVALID_HANDLE_VALUE)
			{
				errmsg="CreateFile of device '"+dl+"' failed. - sendMBR";
				Server->Log(errmsg, LL_ERROR);
				CloseHandle(hVolume);
				return false;
			}

			DWORD numPartitions=10;
			DWORD inf_size=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1);

			std::auto_ptr<DRIVE_LAYOUT_INFORMATION_EX> inf((DRIVE_LAYOUT_INFORMATION_EX*)new char[sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1)]);

			b=DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, inf.get(), inf_size, &ret_bytes, NULL);
			while(b==0 && GetLastError()==ERROR_INSUFFICIENT_BUFFER && numPartitions<1000)
			{
				numPartitions*=2;
				inf_size=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1);
				inf.reset((DRIVE_LAYOUT_INFORMATION_EX*)new char[inf_size]);
				b=DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, inf.get(), inf_size, &ret_bytes, NULL);
			}
			if(b==0)
			{
				errmsg="DeviceIoControl IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed. Volume: '"+dl+"' Error: "+convert((int)GetLastError());
				Server->Log(errmsg, LL_ERROR);
				CloseHandle(hDevice);
				CloseHandle(hVolume);
				return false;
			}
			
			if(inf->PartitionStyle==PARTITION_STYLE_GPT)
			{
				gpt_style=true;
			}
			else if(inf->PartitionStyle!=PARTITION_STYLE_MBR)
			{
				errmsg="Partition style "+convert((unsigned int)inf->PartitionStyle)+" not supported. Volume: '"+dl;
				Server->Log(errmsg, LL_ERROR);
				CloseHandle(hDevice);
				CloseHandle(hVolume);
				return false;
			}

			if(gpt_style)
			{
				STORAGE_PROPERTY_QUERY query = {};
				query.PropertyId = StorageAccessAlignmentProperty;
				query.QueryType = PropertyStandardQuery;

				STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR alignment_descriptor;
				
				b=DeviceIoControl( hVolume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), 
					&alignment_descriptor, sizeof(alignment_descriptor), 
					&ret_bytes,	NULL);

				if(b==FALSE)
				{
					Server->Log("Cannot get physical sector size of volume: '"+dl+". Assuming 512 bytes.", LL_WARNING);
				}
				else
				{
					unsigned int r_logical_sector_size = alignment_descriptor.BytesPerLogicalSector;

					if( (r_logical_sector_size & (r_logical_sector_size-1))==0 &&
						r_logical_sector_size!=0 )
					{
						logical_sector_size = r_logical_sector_size;
					}
				}				
			}

			if(dynamic_volume)
			{
				bool found=false;
				for(DWORD j=0;j<vde->NumberOfDiskExtents;++j)
				{
					for(DWORD i=0;i<inf->PartitionCount;++i)
					{				
						if(inf->PartitionEntry[i].StartingOffset.QuadPart==vde->Extents[j].StartingOffset.QuadPart)
						{
							dev_num.PartitionNumber=inf->PartitionEntry[i].PartitionNumber;
							dev_num.DeviceNumber=vde->Extents[j].DiskNumber;
							found=true;
						}
					}
				}

				if(found)
				{
					errmsg="Dynamic volumes are not supported. It may work with mirrored whole disk volumes though. Volume: '"+dl+"'";
					Server->Log(errmsg, LL_WARNING);
				}
				else
				{
					errmsg="Did not find PartitionNumber of dynamic volume. Volume: '"+dl+"'";
					Server->Log(errmsg, LL_ERROR);
					CloseHandle(hVolume);
					return false;
				}
			}

			CloseHandle(hDevice);						
		}
		else
		{
			errmsg="DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned no extends. Volume: '"+dl+"'";
			Server->Log(errmsg, LL_ERROR);
			CloseHandle(hVolume);
			return false;
		}
	}
	
	CloseHandle(hVolume);

	wchar_t voln[MAX_PATH+1];
	DWORD voln_size=MAX_PATH+1;
	DWORD voln_sern;
	wchar_t fsn[MAX_PATH+1];
	DWORD fsn_size=MAX_PATH+1;
	b=GetVolumeInformationW(Server->ConvertToWchar(dl+"\\").c_str(), voln, voln_size, &voln_sern, NULL, NULL, fsn, fsn_size);
	if(b==0)
	{
		errmsg="GetVolumeInformationW failed. Volume: '"+dl+"'";
		Server->Log(errmsg, LL_ERROR);
		return false;
	}

	CWData mbr;
	mbr.addChar(1);
	mbr.addChar(gpt_style?1:0);
	mbr.addInt(dev_num.DeviceNumber);
	mbr.addInt(dev_num.PartitionNumber);
	mbr.addString(convert((_i64)voln_sern));
	mbr.addString(Server->ConvertFromWchar(voln));
	mbr.addString(Server->ConvertFromWchar(fsn));

	IFile *dev=Server->openFile("\\\\.\\PhysicalDrive"+convert((int)dev_num.DeviceNumber), MODE_READ_DEVICE);

	if(dev==NULL)
	{
		errmsg="Error opening Device "+convert((int)dev_num.DeviceNumber);
		Server->Log(errmsg, LL_ERROR);
		return false;
	}

	std::string mbr_bytes=dev->Read(512);

	mbr.addString(mbr_bytes);

	if(gpt_style)
	{
		Server->Log("GUID partition table found", LL_DEBUG);

		if(!dev->Seek(logical_sector_size))
		{
			errmsg="Error seeking in device to GPT header "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		std::string gpt_header = dev->Read(logical_sector_size);

		if(gpt_header.size()!=logical_sector_size)
		{
			errmsg="Error reading GPT header "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		if(gpt_header.size()<sizeof(EfiHeader))
		{
			errmsg="GPT header too small "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		const EfiHeader* gpt_header_s = reinterpret_cast<const EfiHeader*>(gpt_header.data());

		if(gpt_header_s->signature!=gpt_magic)
		{
			errmsg="GPT magic wrong "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		mbr.addInt64(logical_sector_size);
		mbr.addString(gpt_header);

		int64 paritition_table_pos = gpt_header_s->partition_table_lba*logical_sector_size;
		if(!dev->Seek(paritition_table_pos))
		{
			errmsg="Error seeking in device to GPT partition table "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		_u32 toread = gpt_header_s->num_parition_entries*gpt_header_s->partition_entry_size;
		std::string gpt_table = dev->Read(toread);

		Server->Log("GUID partition table size is "+PrettyPrintBytes(toread), LL_DEBUG);

		if(gpt_table.size()!=toread)
		{
			errmsg="Error reading GPT partition table "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		mbr.addInt64(paritition_table_pos);
		mbr.addString(gpt_table);

		// BACKUP HEADER
		int64 backup_gpt_location = gpt_header_s->backup_lba*logical_sector_size;
		if(!dev->Seek(backup_gpt_location))
		{
			errmsg="Error seeking in device to backup GPT header "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		gpt_header = dev->Read(logical_sector_size);

		if(gpt_header.size()!=logical_sector_size)
		{
			errmsg="Error reading backup GPT header "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		if(gpt_header.size()<sizeof(EfiHeader))
		{
			errmsg="Backup GPT header too small "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		const EfiHeader* backup_gpt_header_s = reinterpret_cast<const EfiHeader*>(gpt_header.data());

		if(backup_gpt_header_s->signature!=gpt_magic)
		{
			errmsg="Backup GPT magic wrong "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		mbr.addInt64(backup_gpt_location);
		mbr.addString(gpt_header);

		paritition_table_pos = backup_gpt_header_s->partition_table_lba*logical_sector_size;
		if(!dev->Seek(paritition_table_pos))
		{
			errmsg="Error seeking in device to GPT partition table "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		toread = backup_gpt_header_s->num_parition_entries*backup_gpt_header_s->partition_entry_size;
		gpt_table = dev->Read(toread);

		if(gpt_table.size()!=toread)
		{
			errmsg="Error reading GPT partition table "+convert((int)dev_num.DeviceNumber);
			Server->Log(errmsg, LL_ERROR);
			return false;
		}

		mbr.addInt64(paritition_table_pos);
		mbr.addString(gpt_table);
	}
	
	mbr.addString((errmsg));

	tcpstack.Send(pipe, mbr);

	return true;
#else //_WIN32
	return false;
#endif
}

const int64 receive_timeouttime=60000;

std::string ClientConnector::receivePacket(const SChannel& channel)
{
    CTCPStack localstack(channel.internet_connection);
	int64 starttime=Server->getTimeMS();
	while(Server->getTimeMS()-starttime<=receive_timeouttime)
	{
        std::string ret;
        size_t rc=channel.pipe->Read(&ret, 10000);
		if(rc==0)
		{
			return "";
		}
        localstack.AddData((char*)ret.c_str(), ret.size());

        ret.clear();
        if(localstack.getPacket(ret))
		{
			return ret;
		}
	}
	return "";
}

void ClientConnector::removeChannelpipe(IPipe *cp)
{
	for (size_t i = 0; i < channel_pipes.size(); ++i)
	{
		if (channel_pipes[i].pipe == cp)
		{
			channel_pipes[i].state = SChannel::EChannelState_Exit;
		}
	}
}

void ClientConnector::downloadImage(str_map params)
{
	_i64 imgsize=-1;
	if(channel_pipes.size()==0)
	{
		imgsize=-2;
		pipe->Write((char*)&imgsize, sizeof(_i64), (int)receive_timeouttime);
		Server->Log("No channel found!", LL_DEBUG);
		return;
	}

	int l_pcdone=0;

	{
		IScopedLock lock(backup_mutex);
		SRunningProcess new_proc;
		new_proc.action = RUNNING_RESTORE_IMAGE;
		new_proc.id = ++curr_backup_running_id;
		new_proc.pcdone = 0;
		
		local_backup_running_id = new_proc.id;

		running_processes.push_back(new_proc);
		status_updated = true;
	}

	ScopedRemoveRunningBackup remove_running_backup(local_backup_running_id);

	for(size_t i=0;i<channel_pipes.size();++i)
	{
		IPipe *c=channel_pipes[i].pipe;
		std::string offset;
		if(params.find("offset")!=params.end())
		{
			offset="&offset="+params["offset"];
		}
		tcpstack.Send(c, "DOWNLOAD IMAGE with_used_bytes=1&img_id="+params["img_id"]+"&time="+params["time"]+"&mbr="+params["mbr"]+offset);

		Server->Log("Downloading from channel "+convert((int)i), LL_DEBUG);

		_i64 imgsize=-1;
		c->Read((char*)&imgsize, sizeof(_i64), 60000);
		Server->Log("Imagesize "+convert(imgsize), LL_DEBUG);
		if(imgsize==-1)
		{
			Server->Log("Error reading size", LL_ERROR);
			if(i+1<channel_pipes.size())
			{
				continue;
			}
			else
			{
				pipe->Write((char*)&imgsize, sizeof(_i64), (int)receive_timeouttime);
				removeChannelpipe(c);
				return;
			}
		}
		
		if(!pipe->Write((char*)&imgsize, sizeof(_i64), (int)receive_timeouttime))
		{
			Server->Log("Could not write to pipe! downloadImage-1", LL_ERROR);
			return;
		}

		const size_t c_buffer_size=32768;
		const unsigned int c_blocksize=4096;
		char buf[c_buffer_size];
		_i64 read=0;

		if(params["mbr"]=="true")
		{
			Server->Log("Downloading MBR...", LL_DEBUG);
			while(read<imgsize)
			{
				size_t c_read=c->Read(buf, c_buffer_size, 60000);
				if(c_read==0)
				{
					Server->Log("Read Timeout", LL_ERROR);
					removeChannelpipe(c);
					return;
				}
				if(!pipe->Write(buf, (_u32)c_read, (int)receive_timeouttime*5))
				{
					Server->Log("Could not write to pipe! downloadImage-5", LL_ERROR);
					removeChannelpipe(c);
					return;
				}
				read+=c_read;
			}
			Server->Log("Downloading MBR done");
			return;
		}

		_i64 used_bytes = imgsize;
		if (channel_pipes[i].restore_version > 0)
		{
			if (!c->Read((char*)&used_bytes, sizeof(_i64), 60000))
			{
				Server->Log("Error getting used bytes", LL_ERROR);
				if (i + 1<channel_pipes.size())
				{
					continue;
				}
				else
				{
					removeChannelpipe(c);
					return;
				}
			}
			Server->Log("Used bytes " + convert(used_bytes), LL_DEBUG);
		}

		unsigned int blockleft=0;
		unsigned int off=0;
		_i64 pos=0;
		_i64 received_bytes = 0;
		while(pos<imgsize)
		{
			size_t r=c->Read(&buf[off], c_buffer_size-off, 180000);
			if( r==0 )
			{
				Server->Log("Read Timeout -2 CS", LL_ERROR);
				removeChannelpipe(c);
				return;
			}
			if(!pipe->Write(&buf[off], r, (int)receive_timeouttime*5))
			{
				Server->Log("Could not write to pipe! downloadImage-3 size "+convert(r)+" off "+convert(off), LL_ERROR);
				removeChannelpipe(c);
				return;
			}
			if(r!=0)
				r+=off;
			off=0;			
			while(true)
			{
				if( blockleft==0 )
				{
					if(r-off>=sizeof(_i64) )
					{
						blockleft=c_blocksize;
						_i64 s;
						memcpy((char*)&s, &buf[off], sizeof(_i64) );
						if(s>imgsize
							&& s!= 0x7fffffffffffffffLL)
						{
							Server->Log("invalid seek value: "+convert(s), LL_ERROR);
						}
						off+=sizeof(_i64);
						pos=s;
					}
					else if(r-off>0)
					{
						memmove(buf, &buf[off], r-off);
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
					unsigned int available=(std::min)((unsigned int)r-off, blockleft);
					read+=available;
					blockleft-=available;
					off+=available;
					received_bytes += available;
					if(off>=r)
					{
						off=0;
						break;
					}
				}
			}

			int t_pcdone=(std::min)((int)100, (int)(((float)received_bytes/(float)used_bytes)*100.f+0.5f));
			if(t_pcdone!=l_pcdone)
			{
				l_pcdone=t_pcdone;
				updateRunningPc(local_backup_running_id, l_pcdone);
			}

			lasttime=Server->getTimeMS();
		}
		remove_running_backup.setSuccess(true);
		Server->Log("Downloading image done", LL_DEBUG);
		return;
	}
	imgsize=-2;
	pipe->Write((char*)&imgsize, sizeof(_i64), (int)receive_timeouttime);
}

void ClientConnector::waitForPings(IScopedLock *lock)
{
	Server->Log("Waiting for pings...", LL_DEBUG);
	while(hasChannelPing())
	{
		lock->relock(NULL);
		Server->wait(10);
		if (run_other != NULL)
		{
			run_other->runOther();
		}
		lock->relock(backup_mutex);
	}
	Server->Log("done. (Waiting for pings)", LL_DEBUG);
}

bool ClientConnector::hasChannelPing()
{
	for (size_t i = 0; i < channel_pipes.size(); ++i)
	{
		if (channel_pipes[i].state == SChannel::EChannelState_Pinging)
		{
			return true;
		}
	}
	return false;
}

int64 ClientConnector::getLastTokenTime(const std::string & tok)
{
	IScopedLock lock(backup_mutex);

	std::map<std::string, int64>::iterator it=last_token_times.find(tok);
	if(it!=last_token_times.end())
	{
		return it->second;
	}
	else
	{
		return 0;
	}
}

void ClientConnector::tochannelSendStartbackup(RunningAction backup_type)
{
	std::string ts;
	if(backup_type==RUNNING_INCR_FILE)
		ts="START BACKUP INCR";
	else if(backup_type==RUNNING_FULL_FILE)
		ts="START BACKUP FULL";
	else if(backup_type==RUNNING_FULL_IMAGE)
		ts="START IMAGE FULL";
	else if(backup_type==RUNNING_INCR_IMAGE)
		ts="START IMAGE INCR";
	else
		return;

	IScopedLock lock(backup_mutex);
	lasttime=Server->getTimeMS();
	if (getActiveProcess(x_pingtimeout) != NULL)
	{
		tcpstack.Send(pipe, "RUNNING");
	}
	else
	{
		bool ok=false;
		if(!channel_pipes.empty())
		{
			CTCPStack tmpstack(channel_pipes.front().internet_connection);
			_u32 rc=(_u32)tmpstack.Send(channel_pipes.front().pipe, ts);
			if(rc!=0)
				ok=true;

			if(!ok)
			{
				tcpstack.Send(pipe, "FAILED");
			}
			else
			{
				tcpstack.Send(pipe, "OK");
			}
		}
		else
		{
			tcpstack.Send(pipe, "NO SERVER");
		}
		
	}
}

void ClientConnector::doQuitClient(void)
{
	do_quit=true;
}

bool ClientConnector::isQuitting(void)
{
	return do_quit;
}

bool ClientConnector::isHashdataOkay(void)
{
	return hashdataok;
}

void ClientConnector::ImageErr(const std::string &msg)
{
	Server->Log(msg, LL_ERROR);
#ifdef _WIN32
	uint64 bs=0xFFFFFFFFFFFFFFFF;
#else
	uint64 bs=0xFFFFFFFFFFFFFFFFLLU;
#endif
	char *buffer=new char[sizeof(uint64)+msg.size()];
	memcpy(buffer, &bs, sizeof(uint64) );
	memcpy(&buffer[sizeof(uint64)], msg.c_str(), msg.size());
	pipe->Write(buffer, sizeof(uint64)+msg.size());
	delete [] buffer;
}

void ClientConnector::setIsInternetConnection(void)
{
	internet_conn=true;
	tcpstack.setAddChecksum(true);
}

void ClientConnector::update_silent(void)
{
	Server->getThreadPool()->execute(new UpdateSilentThread(), "silent client update");
}

bool ClientConnector::calculateFilehashesOnClient(const std::string& clientsubname)
{
	if(internet_conn)
	{
		std::string settings_fn = "urbackup/data/settings.cfg";
		if(!clientsubname.empty())
		{
			settings_fn = "urbackup/data/settings_"+clientsubname+".cfg";
		}
		ISettingsReader *curr_settings=Server->createFileSettingsReader(settings_fn);

		std::string val;
		if(curr_settings->getValue("internet_calculate_filehashes_on_client", &val)
			|| curr_settings->getValue("internet_calculate_filehashes_on_client_def", &val))
		{
			if(val=="true")
			{
				Server->destroy(curr_settings);
				return true;
			}
		}

		Server->destroy(curr_settings);
	}

	return false;
}

bool ClientConnector::isBackupRunning()
{
	IScopedLock lock(backup_mutex);

	int pcdone;
	std::string job = getCurrRunningJob(false, pcdone);

	return job!="NOA";
}

bool ClientConnector::tochannelSendChanges( const char* changes, size_t changes_size)
{
	IScopedLock lock(backup_mutex);

	if(channel_pipes.empty())
	{
		has_file_changes = true;
		return false;
	}

	std::string changes_str = "CHANGES "+std::string(changes, changes+changes_size);

	CTCPStack tmpstack(channel_pipes.front().internet_connection);
	if(tmpstack.Send(channel_pipes.front().pipe, changes_str)!=changes_str.size())
	{
		has_file_changes = true;
		return false;
	}

	return true;
}


int ClientConnector::getCapabilities()
{
	int capa=0;
	if(channel_pipes.size()==0)
	{
		capa=last_capa;
		capa|=DONT_ALLOW_STARTING_FILE_BACKUPS;
		capa|=DONT_ALLOW_STARTING_IMAGE_BACKUPS;
	}
	else
	{
		capa=INT_MAX;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
			capa=capa & channel_pipes[i].capa;
		}

		if(capa!=last_capa)
		{
			IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
			IQuery *cq=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='last_capa'", false);
			if(cq!=NULL)
			{
				cq->Bind(capa);
				if(cq->Write(0))
				{
					last_capa=capa;
				}
				cq->Reset();				
				db->destroyQuery(cq);
			}
		}
	}
	return capa;
}

bool ClientConnector::multipleChannelServers()
{
	if (channel_pipes.size() <= 1)
	{
		return false;
	}

	std::string last_token = channel_pipes[0].token;
	for (size_t i = 1; i < channel_pipes.size(); ++i)
	{
		if (last_token != channel_pipes[i].token)
		{
			return true;
		}
	}
	return false;
}

IPipe* ClientConnector::getFileServConnection(const std::string& server_token, unsigned int timeoutms)
{
	IScopedLock lock(backup_mutex);

	int64 starttime = Server->getTimeMS();

	do 
	{
		for(size_t i=0;i<channel_pipes.size();++i)
		{
			if(channel_pipes[i].make_fileserv!=NULL &&
				channel_pipes[i].token==server_token &&
				!(*channel_pipes[i].make_fileserv))
			{
				*channel_pipes[i].make_fileserv=true;
			}
		}

		lock.relock(NULL);
		Server->wait(100);
		lock.relock(backup_mutex);

		for(size_t i=0;i<fileserv_connections.size();++i)
		{
			if(fileserv_connections[i].token==server_token )
			{
				IPipe* ret = fileserv_connections[i].pipe;
				fileserv_connections.erase(fileserv_connections.begin()+i);
				return ret;
			}
		}

	} while (Server->getTimeMS()-starttime<timeoutms);

	Server->Log("Timeout while getting a fileserv connection");

	return NULL;	
}

bool ClientConnector::closeSocket( void )
{
	if(state!=CCSTATE_FILESERV)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void ClientConnector::sendStatus()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	IScopedLock lock(backup_mutex);

	state = CCSTATE_STATUS;

	int64 last_backup_time = getLastBackupTime();

	std::string ret;
	if (last_backup_time > 0)
	{
		ret += convert(last_backup_time);
	}

	int pcdone = -1;
	ret += "#" + getCurrRunningJob(true, pcdone);

	ret+="#"+convert(pcdone);

	if(IdleCheckerThread::getPause())
	{
		ret+="#P";
	}
	else
	{
		ret+="#NP";
	}

	ret+="#capa="+convert(getCapabilities());

	{
		IScopedLock lock(ident_mutex);
		if(!new_server_idents.empty())
		{
			ret+="&new_ident="+new_server_idents[new_server_idents.size()-1];
			new_server_idents.erase(new_server_idents.begin()+new_server_idents.size()-1);
		}
	}

	ret+="&has_server=";
	if(channel_pipes.empty())
	{
		ret+="false";
	}
	else
	{
		ret+="true";
	}

	if(restore_ok_status==RestoreOk_Wait)
	{
		ret+="&restore_ask="+convert(ask_restore_ok);

		if (restore_files != NULL)
		{
			if (restore_files->is_single_file())
			{
				ret += "&restore_file=true";
			}
			else
			{
				ret += "&restore_file=false";
			}

			ret += "&restore_path=" + EscapeParamString(restore_files->get_restore_path());

			ret += "&process_id=" + convert(restore_files->get_local_process_id());
		}
	}

	if(needs_restore_restart>0)
	{
		ret+="&needs_restore_restart="+convert(needs_restore_restart);
	}

	tcpstack.Send(pipe, ret);

	db->destroyAllQueries();
}

bool ClientConnector::tochannelLog(int64 log_id, const std::string& msg, int loglevel, const std::string& identity)
{
	return sendMessageToChannel("LOG "+convert(log_id)+"-"+convert(loglevel)+"-"+msg, 10000, identity);
}

void ClientConnector::updateRestorePc(int64 local_process_id, int64 restore_id, int64 status_id, int nv, const std::string& identity,
	const std::string& fn, int fn_pc, int64 total_bytes, int64 done_bytes, double speed_bpms)
{
	IScopedLock lock(backup_mutex);

	{
		IScopedLock lock_process(process_mutex);

		SRunningProcess* proc = getRunningProcess(local_process_id);

		if (proc != NULL)
		{
			if (nv > 100)
			{
				removeRunningProcess(local_process_id, total_bytes==done_bytes);
			}
			else
			{
				proc->pcdone = nv;
				proc->last_pingtime = Server->getTimeMS();
				proc->details = fn;
				proc->detail_pc = fn_pc;
				proc->total_bytes = total_bytes;
				proc->done_bytes = done_bytes;
				proc->speed_bpms = speed_bpms;
			}
		}
	}

	sendMessageToChannel("RESTORE PERCENT pc="+convert(nv)+"&status_id="+convert(status_id)+"&id="+convert(restore_id)
		+"&details="+EscapeParamString(fn)+"&detail_pc="+convert(fn_pc)+"&total_bytes="+convert(total_bytes)+"&done_bytes="+convert(done_bytes)
		+"&speed_bpms="+convert(speed_bpms),
		0, identity);
}

bool ClientConnector::restoreDone( int64 log_id, int64 status_id, int64 restore_id, bool success, const std::string& identity )
{
	return sendMessageToChannel("RESTORE DONE status_id="+convert(status_id)+
		"&log_id=" + convert(log_id) +
		"&id=" + convert(restore_id) +
		"&success=" + convert(success), 60000, identity);
}

bool ClientConnector::sendMessageToChannel( const std::string& msg, int timeoutms, const std::string& identity )
{
	IScopedLock lock(backup_mutex);

	int64 starttime = Server->getTimeMS();

	do
	{
		for(size_t i=0;i<channel_pipes.size();++i)
		{
			if(channel_pipes[i].token == identity)
			{
				CTCPStack tmpstack(channel_pipes[i].internet_connection);
				if(tmpstack.Send(channel_pipes[i].pipe, msg)!=msg.size())
				{
					return false;
				}

				return true;
			}
		}

		lock.relock(NULL);
		Server->wait(100);
		lock.relock(backup_mutex);
	} while(Server->getTimeMS()-starttime<timeoutms);

	return false;
}

std::string ClientConnector::getAccessTokensParams(const std::string& tokens, bool with_clientname, const std::string& virtual_client)
{
    if(tokens.empty())
    {
        return std::string();
    }

	std::auto_ptr<ISettingsReader> access_keys(
		Server->createFileSettingsReader("urbackup/access_keys.properties"));

	std::vector<std::string> server_token_keys = access_keys->getKeys();

	if(server_token_keys.empty())
	{
		Server->Log("No access key present", LL_ERROR);
		return std::string();
	}

	std::string ret;

	std::string session_key;
	session_key.resize(32);
	Server->secureRandomFill(&session_key[0], session_key.size());

	bool has_token=false;
	for(size_t i=0;i<server_token_keys.size();++i)
	{
		if (!next(server_token_keys[i], 0, "key.")
			&& !next(server_token_keys[i], 0, "last.key.") )
		{
			continue;
		}

		std::string server_key;

		if(access_keys->getValue(server_token_keys[i],
			&server_key) && !server_key.empty())
		{
			ret += "&tokens"+convert(i)+"="+base64_encode_dash(
				crypto_fak->encryptAuthenticatedAES(session_key,
				server_key, 1));
			has_token=true;
		}
	}

	if(has_token)
	{
		ret += "&token_data="+base64_encode_dash(
			crypto_fak->encryptAuthenticatedAES(tokens,
			session_key, 1) );
	}	

	if(with_clientname)
	{
		std::auto_ptr<ISettingsReader> settings(
			Server->createFileSettingsReader("urbackup/data/settings.cfg"));

		std::string computername;
        if( !settings->getValue("computername", &computername) )
        {
            settings->getValue("computername_def", &computername);
        }
        if(computername.empty())
        {
            computername = IndexThread::getFileSrv()->getServerName();
        }

        if(!computername.empty())
        {
			if (virtual_client.empty())
			{
				computername += "[" + virtual_client + "]";
			}

            ret+="&clientname="+EscapeParamString(computername);
		}
	}

    return ret;
}

bool ClientConnector::sendChannelPacket(const SChannel& channel, const std::string& msg)
{
    CTCPStack localstack(channel.internet_connection);
    return localstack.Send(channel.pipe, msg) == msg.size();
}

bool ClientConnector::versionNeedsUpdate(const std::string & local_version, const std::string & server_version)
{
	std::vector<std::string> local_features;
	int ilocal_version = parseVersion(local_version, local_features);
	std::vector<std::string> server_features;
	int iserver_version = parseVersion(server_version, server_features);

	for (size_t i = 0; i < local_features.size(); ++i)
	{
		if (std::find(server_features.begin(), server_features.end(), local_features[i]) == server_features.end())
		{
			Server->Log("Server update does not have feature " + local_features[i] + ". Not updating.", LL_INFO);
			return false;
		}
	}

	if (iserver_version > ilocal_version)
	{
		Server->Log("Server has new version "+convert(iserver_version)+" (client version: "+convert(ilocal_version)+"). Updating...", LL_INFO);
		return true;
	}

	for (size_t i = 0; i < server_features.size(); ++i)
	{
		if (std::find(local_features.begin(), local_features.end(), server_features[i]) == local_features.end())
		{
			Server->Log("Client currently does not have feature " + server_features[i] + ". Updating...", LL_INFO);
			return true;
		}
	}

	return false;
}

int ClientConnector::parseVersion(const std::string & version, std::vector<std::string>& features)
{
	if (version.find("-") == std::string::npos)
	{
		return atoi(version.c_str());
	}
	else
	{
		TokenizeMail(getafter("-", version), features, ",");

		return atoi(getuntil("-", version).c_str());
	}
}

void ClientConnector::requestRestoreRestart()
{
	IScopedLock lock(backup_mutex);
	++needs_restore_restart;
	status_updated=true;
}

std::string ClientConnector::getHasNoRecentBackup()
{
	IScopedLock lock(backup_mutex);

	static int64 last_monotonic_time = Server->getTimeMS();
	static int64 last_nonmonotonic_time = Server->getTimeSeconds();

	int64 cmontime = Server->getTimeMS();
	int64 cnmontime = Server->getTimeSeconds();

	int64 monotonic_passed_time = cmontime - last_monotonic_time;
	int64 nonmonotonic_passed_time = cnmontime - last_nonmonotonic_time;

	last_monotonic_time = cmontime;
	last_nonmonotonic_time = cnmontime;

	if (monotonic_passed_time+60000 < nonmonotonic_passed_time * 1000)
	{
		Server->Log("Detected forward time jump. Resetting system start time.");
		service_starttime = cmontime;
	}

	int64 last_backup_time = getLastBackupTime();

	if(last_backup_time==0)
	{
		return "NO_RECENT";
	}

	if(Server->getTimeMS()-service_starttime<backup_alert_delay)
	{
		return "NOA";
	}

	if(Server->getTimeSeconds()-last_backup_time<= backup_interval)
	{
		return "NOA";
	}

	return "NO_RECENT";
}


void ClientConnector::refreshSessionFromChannel(const std::string& endpoint_name)
{
	IScopedLock lock(backup_mutex);

	for (size_t i = 0; i<channel_pipes.size(); ++i)
	{
		if (channel_pipes[i].pipe == pipe)
		{
			if (!channel_pipes[i].server_identity.empty())
			{
				ServerIdentityMgr::checkServerSessionIdentity(channel_pipes[i].server_identity, endpoint_name);
			}
			break;
		}
	}
}

void ClientConnector::timeoutAsyncFileIndex()
{
	int64 ctime = Server->getTimeMS();
	IScopedLock lock(backup_mutex);
	for (std::map<std::string, SAsyncFileList>::iterator it = async_file_index.begin();
		it != async_file_index.end();)
	{
		if (ctime - it->second.last_update > async_index_timeout * 2)
		{
			std::map<std::string, SAsyncFileList>::iterator it_curr = it;
			++it;
			async_file_index.erase(it_curr);
		}
		else
		{
			++it;
		}
	}
}

SRunningProcess * ClientConnector::getRunningProcess(RunningAction action, std::string server_token)
{
	for (size_t i = 0; i < running_processes.size(); ++i)
	{
		SRunningProcess& curr = running_processes[i];
		if (curr.action == action
			&& (curr.server_token == server_token || curr.server_token.empty() || server_token.empty())
			&& curr.server_id == 0)
		{
			return &curr;
		}
	}

	return NULL;
}

SRunningProcess * ClientConnector::getRunningFileBackupProcess(std::string server_token, int64 server_id)
{
	for (size_t i = 0; i < running_processes.size(); ++i)
	{
		SRunningProcess& curr = running_processes[i];
		if ((curr.action == RUNNING_FULL_FILE || curr.action == RUNNING_INCR_FILE || curr.action == RUNNING_RESUME_FULL_FILE || curr.action == RUNNING_RESUME_INCR_FILE)
			&& (curr.server_token == server_token || curr.server_token.empty() || server_token.empty())
			&& ((curr.server_id == 0 && server_id == 0) || curr.server_id == server_id))
		{
			return &curr;
		}
	}

	return NULL;
}

SRunningProcess * ClientConnector::getRunningBackupProcess(std::string server_token, int64 server_id)
{
	for (size_t i = running_processes.size(); i-- > 0;)
	{
		SRunningProcess& curr = running_processes[i];
		if ( (curr.server_token == server_token || curr.server_token.empty() || server_token.empty())
			&& ((curr.server_id == 0 && server_id == 0) || curr.server_id == server_id))
		{
			return &curr;
		}
	}

	return NULL;
}

SRunningProcess * ClientConnector::getRunningProcess(int64 id)
{
	for (size_t i = 0; i < running_processes.size(); ++i)
	{
		SRunningProcess& curr = running_processes[i];
		if (curr.id == id)
		{
			return &curr;
		}
	}

	return NULL;
}

SRunningProcess * ClientConnector::getActiveProcess(int64 timeout)
{
	int64 ctime = Server->getTimeMS();
	for (size_t i = 0; i < running_processes.size(); ++i)
	{
		SRunningProcess& curr = running_processes[i];
		if (ctime - curr.last_pingtime<timeout)
		{
			return &curr;
		}
	}

	return NULL;
}

bool ClientConnector::removeRunningProcess(int64 id, bool success)
{
	IScopedLock lock(process_mutex);

	for (size_t i = 0; i < running_processes.size(); ++i)
	{
		SRunningProcess& curr = running_processes[i];
		if (curr.id == id)
		{
			finished_processes.push_back(SFinishedProcess(id, success));
			while (finished_processes.size() > 20)
			{
				finished_processes.erase(finished_processes.begin());
			}
			running_processes.erase(running_processes.begin() + i);
			return true;
		}
	}

	return false;
}

void ClientConnector::timeoutFilesrvConnections()
{
	IScopedLock lock(backup_mutex);

	for (size_t i = 0; i < fileserv_connections.size();)
	{
		if (Server->getTimeMS() - fileserv_connections[i].starttime>60000)
		{
			Server->destroy(fileserv_connections[i].pipe);
			fileserv_connections.erase(fileserv_connections.begin() + i);
		}
		else
		{
			++i;
		}
	}
}

bool ClientConnector::updateRunningPc(int64 id, int pcdone)
{
	IScopedLock lock(process_mutex);

	SRunningProcess* proc = getRunningProcess(id);
	if (proc == NULL)
	{
		return false;
	}

	if (proc->pcdone != pcdone)
	{
		status_updated = true;
	}
	proc->pcdone = pcdone;

	return true;
}

std::string ClientConnector::actionToStr(RunningAction action)
{
	switch (action)
	{
	case RUNNING_INCR_FILE:
		return "INCR";
	case RUNNING_FULL_FILE:
		return "FULL";
	case RUNNING_FULL_IMAGE:
		return "FULLI";
	case RUNNING_INCR_IMAGE:
		return "INCRI";
	case RUNNING_RESUME_INCR_FILE:
		return "R_INCR";
	case RUNNING_RESUME_FULL_FILE:
		return "R_FULL";
	case RUNNING_RESTORE_IMAGE:
		return "RESTORE_IMAGE";
	case RUNNING_RESTORE_FILE:
		return "RESTORE_FILES";
	default:
		return "";
	}
}

void ClientConnector::removeTimedOutProcesses(std::string server_token, bool file)
{
	int64 ctime = Server->getTimeMS();

	for (size_t i = 0; i < running_processes.size();)
	{
		SRunningProcess& curr = running_processes[i];

		bool curr_file = (curr.action == RUNNING_FULL_FILE || curr.action == RUNNING_INCR_FILE || curr.action == RUNNING_RESUME_FULL_FILE || curr.action == RUNNING_RESUME_INCR_FILE);

		if ( (curr_file || !file)
			&& (curr.server_token == server_token || curr.server_token.empty() || server_token.empty())
			&& ctime - curr.last_pingtime>x_pingtimeout )
		{
			running_processes.erase(running_processes.begin() + i);
			continue;
		}

		++i;
	}
}

int64 ClientConnector::addNewProcess(SRunningProcess proc)
{
	IScopedLock lock(process_mutex);
	if (proc.id == 0)
	{
		proc.id = ++curr_backup_running_id;
	}	
	running_processes.push_back(proc);
	return proc.id;
}


