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

#include "ClientService.h"
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

#include <memory.h>
#include <stdlib.h>
#include <limits.h>
#include <memory>
#include <algorithm>

#ifndef _WIN32
#define _atoi64 atoll
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

RunningAction ClientConnector::backup_running=RUNNING_NONE;
volatile bool ClientConnector::backup_done=false;
IMutex *ClientConnector::backup_mutex=NULL;
unsigned int ClientConnector::incr_update_intervall=0;
int64 ClientConnector::last_pingtime=0;
SChannel ClientConnector::channel_pipe;
int ClientConnector::pcdone=0;
int64 ClientConnector::eta_ms=0;
int ClientConnector::pcdone2=0;
std::vector<SChannel> ClientConnector::channel_pipes;
std::vector<IPipe*> ClientConnector::channel_exit;
std::vector<IPipe*> ClientConnector::channel_ping;
std::vector<int> ClientConnector::channel_capa;
IMutex *ClientConnector::progress_mutex=NULL;
volatile bool ClientConnector::img_download_running=false;
db_results ClientConnector::cached_status;
std::string ClientConnector::backup_source_token;
std::map<std::string, int64> ClientConnector::last_token_times;
int ClientConnector::last_capa=0;
IMutex *ClientConnector::ident_mutex=NULL;
std::vector<std::string> ClientConnector::new_server_idents;
bool ClientConnector::end_to_end_file_backup_verification_enabled=false;
std::map<std::string, std::string> ClientConnector::challenges;

#ifdef _WIN32
const std::string pw_file="pw.txt";
const std::string pw_change_file="pw_change.txt";
#else
const std::string pw_file="urbackup/pw.txt";
const std::string pw_change_file="urbackup/pw_change.txt";
#endif

#ifdef _WIN32
class UpdateSilentThread : public IThread
{
public:
	void operator()(void)
	{
		Server->wait(2*60*1000); //2min

		STARTUPINFOW si;
		PROCESS_INFORMATION pi;
		memset(&si, 0, sizeof(STARTUPINFO) );
		memset(&pi, 0, sizeof(PROCESS_INFORMATION) );
		si.cb=sizeof(STARTUPINFO);
		if(!CreateProcessW(L"UrBackupUpdate.exe", L"UrBackupUpdate.exe /S", NULL, NULL, false, NORMAL_PRIORITY_CLASS|CREATE_NO_WINDOW, NULL, NULL, &si, &pi) )
		{
			Server->Log("Executing silent update failed: "+nconvert((int)GetLastError()), LL_ERROR);
		}
		else
		{
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}

		delete this;
	}
};
#endif

void ClientConnector::init_mutex(void)
{
	if(backup_mutex==NULL)
	{
		backup_mutex=Server->createMutex();
		progress_mutex=Server->createMutex();
		ident_mutex=Server->createMutex();
	}
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	db_results res=db->Read("SELECT tvalue FROM misc WHERE tkey='last_capa'");
	if(!res.empty())
	{
		last_capa=watoi(res[0][L"last_capa"]);
	}
	else
	{
		db->Write("INSERT INTO misc (tkey, tvalue) VALUES ('last_capa', '0');");
		last_capa=0;
	}
}

void ClientConnector::destroy_mutex(void)
{
	Server->destroy(backup_mutex);
	Server->destroy(ident_mutex);
	Server->destroy(progress_mutex);
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

bool ClientConnector::Run(void)
{
	if(do_quit)
	{
		if(state!=CCSTATE_START_FILEBACKUP && state!=CCSTATE_SHADOWCOPY && state!=CCSTATE_WAIT_FOR_CONTRACTORS)
		{
			if(is_channel)
			{
				IScopedLock lock(backup_mutex);
				if(channel_pipe.pipe==pipe)
				{
					channel_pipe=SChannel();
				}
				for(size_t i=0;i<channel_pipes.size();++i)
				{
					if(channel_pipes[i].pipe==pipe)
					{
						channel_pipes.erase(channel_pipes.begin()+i);
						channel_capa.erase(channel_capa.begin()+i);
						break;
					}
				}
				for(size_t i=0;i<channel_ping.size();++i)
				{
					if(channel_ping[i]==pipe)
					{
						channel_ping.erase(channel_ping.begin()+i);
						break;
					}
				}
			}
			if(waitForThread())
			{
				Server->wait(10);
				do_quit=true;
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
		if(Server->getTimeMS()-lasttime>10000)
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
	case CCSTATE_START_FILEBACKUP:
		{
			std::string msg;
			mempipe->Read(&msg, 0);
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
				backup_running=RUNNING_NONE;
				backup_done=true;
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
				backup_running=RUNNING_NONE;
				backup_done=true;
			}
			else if(file_version>1 && Server->getTimeMS()-last_update_time>30000)
			{
				last_update_time=Server->getTimeMS();
				tcpstack.Send(pipe, "BUSY");
			}
		}break;
	case CCSTATE_SHADOWCOPY:
		{
			std::string msg;
			mempipe->Read(&msg, 0);
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
				return false;
			}
			else if(msg.find("done")==0)
			{
				mempipe->Write("exit");
				mempipe=Server->createMemoryPipe();
				mempipe_owner=true;
				tcpstack.Send(pipe, "DONE");
				lasttime=Server->getTimeMS();
				state=CCSTATE_NORMAL;
			}
			else if(msg.find("failed")==0)
			{
				mempipe->Write("exit");
				mempipe=Server->createMemoryPipe();
				mempipe_owner=true;
				tcpstack.Send(pipe, "FAILED");
				lasttime=Server->getTimeMS();
				state=CCSTATE_NORMAL;
			}
		}break;
	case CCSTATE_CHANNEL: //Channel
		{		
			IScopedLock lock(backup_mutex);
			if(Server->getTimeMS()-lasttime>180000)
			{
				Server->Log("Client timeout in ClientConnector::Run - Channel", LL_DEBUG);
				{
					if(channel_pipe.pipe==pipe)
						channel_pipe=SChannel();

					for(size_t i=0;i<channel_pipes.size();++i)
					{
						if(channel_pipes[i].pipe==pipe)
						{
							channel_pipes.erase(channel_pipes.begin()+i);
							channel_capa.erase(channel_capa.begin()+i);
							break;
						}
					}
					for(size_t i=0;i<channel_ping.size();++i)
					{
						if(channel_ping[i]==pipe)
						{
							channel_ping.erase(channel_ping.begin()+i);
							break;
						}
					}
				}
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				return false;
			}
			for(size_t i=0;i<channel_exit.size();++i)
			{
				if(channel_exit[i]==pipe)
				{
					do_quit=true;
					channel_exit.erase(channel_exit.begin()+i);
					break;
				}
			}
			if(Server->getTimeMS()-last_channel_ping>60000)
			{
				bool found=false;
				for(size_t i=0;i<channel_ping.size();++i)
				{
					if(channel_ping[i]==pipe)
					{
						found=true;
						break;
					}
				}
				if(!found)
				{
					channel_ping.push_back(pipe);
				}
				tcpstack.Send(pipe, "PING");
				last_channel_ping=Server->getTimeMS();
			}
		}break;
	case CCSTATE_IMAGE:
	case CCSTATE_IMAGE_HASHDATA:
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
					writeUpdateFile(hashdatafile, "version_new.txt");
					writeUpdateFile(hashdatafile, "UrBackupUpdate.sig");
					writeUpdateFile(hashdatafile, "UrBackupUpdate_untested.exe");

					std::wstring hashdatafile_fn=hashdatafile->getFilenameW();
					Server->destroy(hashdatafile);
					Server->deleteFile(hashdatafile_fn);

					if(crypto_fak!=NULL)
					{
						IFile *updatefile=Server->openFile("UrBackupUpdate_untested.exe");
						if(updatefile!=NULL)
						{
							if(checkHash(getSha512Hash(updatefile)))
							{
								Server->destroy(updatefile);
								if(crypto_fak->verifyFile("urbackup_dsa.pub", "UrBackupUpdate_untested.exe", "UrBackupUpdate.sig"))
								{
									Server->deleteFile("UrBackupUpdate.exe");
									moveFile(L"UrBackupUpdate_untested.exe", L"UrBackupUpdate.exe");
									
									tcpstack.Send(pipe, "ok");

									if(silent_update)
									{
										update_silent();
									}
									else
									{
										Server->deleteFile("version.txt");
										moveFile(L"version_new.txt", L"version.txt");
									}
								}
								else
								{									
									Server->Log("Verifying update file failed. Signature did not match", LL_ERROR);
									Server->deleteFile("UrBackupUpdate_untested.exe");
									tcpstack.Send(pipe, "verify_sig_err");
								}
							}
							else
							{
								Server->destroy(updatefile);
								Server->Log("Verifing update file failed. Update was installed previously", LL_ERROR);
								Server->deleteFile("UrBackupUpdate_untested.exe");
								tcpstack.Send(pipe, "verify_sig_already_used_err");
							}
						}
					}
					else
					{
						Server->Log("Verifing update file failed. Cryptomodule not present", LL_ERROR);
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
	std::string prev_h=getFile("updates_h.dat");
	int lc=linecount(prev_h);
	for(int i=0;i<lc;++i)
	{
		std::string l=strlower(trim(getline(i, prev_h)));
		if(l==shah)
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

void ClientConnector::ReceivePackets(void)
{
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
			Server->Log("rc=0 hasError="+nconvert(pipe->hasError())+" state="+nconvert(state), LL_DEBUG);
#ifdef _WIN32
#ifdef _DEBUG
			Server->Log("Err: "+nconvert((int)GetLastError()), LL_DEBUG);
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
	if(state==CCSTATE_IMAGE_HASHDATA || state==CCSTATE_UPDATE_DATA)
	{
		lasttime=Server->getTimeMS();

		if(hashdatafile->Write(cmd)!=cmd.size())
		{
			Server->Log("Error writing to hashdata temporary file", LL_ERROR);
			do_quit=true;
			return;
		}
		if(hashdataleft>=cmd.size())
		{
			hashdataleft-=(_u32)cmd.size();
			//Server->Log("Hashdataleft: "+nconvert(hashdataleft), LL_DEBUG);
		}
		else
		{
			Server->Log("Too much hashdata - error", LL_ERROR);
		}

		if(hashdataleft==0)
		{
			hashdataok=true;
			if(state==CCSTATE_IMAGE_HASHDATA)
				state=CCSTATE_IMAGE;
			else if(state==CCSTATE_UPDATE_DATA)
				state=CCSTATE_UPDATE_FINISH;
		}

		return;
	}

	tcpstack.AddData((char*)cmd.c_str(), cmd.size());

	size_t packetsize;
	char *ccstr;
	while( (ccstr=tcpstack.getPacket(&packetsize))!=NULL)
	{
		cmd.resize(packetsize);
		if(packetsize>0)
		{
			memcpy(&cmd[0], ccstr, packetsize);
		}
		delete [] ccstr;
		
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
			if(!checkPassword(params[L"pw"], pw_change_ok))
			{
				Server->Log("Password wrong!", LL_ERROR);
				continue;
			}
			else
			{
				pw_ok=true;
			}
		}

		if(!identity.empty() && ServerIdentityMgr::checkServerSessionIdentity(identity))
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
		else if(cmd=="GET CHALLENGE")
		{
			CMD_GET_CHALLENGE(identity); continue;
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
			else if( (cmd=="CHANNEL" || next(cmd, 0, "1CHANNEL") ) )
			{
				CMD_CHANNEL(cmd, &g_lock); continue;
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
			else if( next(cmd, 0, "CLIENTUPDATE ") || next(cmd, 0, "1CLIENTUPDATE ") )
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
		}
		if(pw_ok) //Commands from client frontend
		{
			if(pw_change_ok) //Administrator commands
			{
				if( cmd=="1GET BACKUP DIRS" || cmd=="GET BACKUP DIRS" )
				{
					CMD_GET_BACKUPDIRS(cmd); continue;
				}
				else if(cmd=="SAVE BACKUP DIRS" )
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
			}
			
			if(cmd=="GET INCRINTERVALL" )
			{
				CMD_GET_INCRINTERVAL(cmd); continue;
			}
			else if(cmd=="STATUS" )
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
			else if( cmd=="GET DOWNLOADPROGRESS" )
			{
				CMD_RESTORE_DOWNLOADPROGRESS(cmd); continue;		
			}
			else if( cmd=="GET ACCESS PARAMETERS")
			{
				CMD_GET_ACCESS_PARAMS(params); continue;
			}
		}
		if(is_channel) //Channel commands from server
		{
			if(cmd=="PONG" )
			{
				CMD_CHANNEL_PONG(cmd); continue;
			}
			else if(cmd=="PING" )
			{
				CMD_CHANNEL_PING(cmd); continue;
			}
		}
		
		tcpstack.Send(pipe, "ERR");
	}
}

bool ClientConnector::checkPassword(const std::wstring &pw, bool& change_pw)
{
	static std::string stored_pw=getFile(pw_file);
	static std::string stored_pw_change=getFile(pw_change_file);
	std::string utf8_pw = Server->ConvertToUTF8(pw);
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

std::wstring removeChars(std::wstring in)
{
	wchar_t illegalchars[] = {'*', ':', '/' , '\\'};
	std::wstring ret;
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

bool ClientConnector::saveBackupDirs(str_map &args, bool server_default)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	db->BeginTransaction();
	db_results backupdirs=db->Prepare("SELECT name, path FROM backupdirs")->Read();
	db->Prepare("DELETE FROM backupdirs")->Write();
	IQuery *q=db->Prepare("INSERT INTO backupdirs (name, path, server_default, optional) VALUES (?, ? ,"+nconvert(server_default?1:0)+", ?)");
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
	std::wstring dir;
	size_t i=0;
	std::vector<std::wstring> new_watchdirs;
	do
	{
		dir=args[L"dir_"+convert(i)];
		if(!dir.empty())
		{
			std::wstring name;
			str_map::iterator name_arg=args.find(L"dir_"+convert(i)+L"_name");
			if(name_arg!=args.end() && !name_arg->second.empty())
				name=name_arg->second;
			else
				name=ExtractFileName(dir);

			bool optional = false;
			size_t optional_off = name.find(L"/optional");
			if(optional_off!=std::string::npos &&
				optional_off == name.size()-9)
			{
				optional=true;
				name.resize(optional_off);
			}

			name=removeChars(name);

			if(dir[dir.size()-1]=='\\' || dir[dir.size()-1]=='/' )
				dir.erase(dir.size()-1,1);

			q2->Bind(name);
			if(q2->Read().empty()==false)
			{
				for(int k=0;k<100;++k)
				{
					q2->Reset();
					q2->Bind(name+L"_"+convert(k));
					if(q2->Read().empty()==true)
					{
						name+=L"_"+convert(k);
						break;
					}
				}
			}
			q2->Reset();

			bool found=false;
			for(size_t i=0;i<backupdirs.size();++i)
			{
				if(backupdirs[i][L"path"]==dir)
				{
					backupdirs[i][L"need"]=L"1";
					found=true;
					break;
				}
			}

			if(!found)
			{
				//It's not already watched. Add it
				new_watchdirs.push_back(dir);
			}
			
			q->Bind(name);
			q->Bind(dir);
			q->Bind(optional?1:0);
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
		data.addChar(5);
		data.addVoidPtr(contractor);
		data.addString(Server->ConvertToUTF8(new_watchdirs[i]));
		IndexThread::stopIndex();
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
		contractors.push_back(contractor);
		state=CCSTATE_WAIT_FOR_CONTRACTORS;
		want_receive=false;
	}

	for(size_t i=0;i<backupdirs.size();++i)
	{
		if(backupdirs[i][L"need"]!=L"1" && backupdirs[i][L"path"]!=L"*")
		{
			//Delete the watch
			IPipe *contractor=Server->createMemoryPipe();
			CWData data;
			data.addChar(6);
			data.addVoidPtr(contractor);
			data.addString(Server->ConvertToUTF8(backupdirs[i][L"path"]));
			IndexThread::stopIndex();
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			contractors.push_back(contractor);
			state=CCSTATE_WAIT_FOR_CONTRACTORS;
			want_receive=false;
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

std::vector<std::wstring> getSettingsList(void);

void ClientConnector::updateSettings(const std::string &pData)
{
	std::auto_ptr<ISettingsReader> curr_settings(Server->createFileSettingsReader("urbackup/data/settings.cfg"));
	std::auto_ptr<ISettingsReader> new_settings(Server->createMemorySettingsReader(pData));

	std::vector<std::wstring> settings_names=getSettingsList();
	settings_names.push_back(L"client_set_settings");
	settings_names.push_back(L"client_set_settings_time");
	std::wstring new_settings_str=L"";
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
		std::wstring key=settings_names[i];

		std::wstring v;
		std::wstring def_v;
		curr_settings->getValue(key+L"_def", def_v);

		if(!curr_settings->getValue(key, &v) )
		{
			std::wstring nv;
			std::wstring new_key;
			if(new_settings->getValue(key, &nv) )
			{
				new_settings_str+=key+L"="+nv+L"\n";
				mod=true;
			}
			if(new_settings->getValue(key+L"_def", &nv) )
			{
				new_settings_str+=key+L"_def="+nv+L"\n";
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
				std::wstring orig_v;
				std::wstring nv;
				if(new_settings->getValue(key+L"_orig", &orig_v) &&
					orig_v==v &&
					(new_settings->getValue(key, &nv) ||
					 new_settings->getValue(key+L"_def", &nv) ) )
				{
					new_settings_str+=key+L"="+nv+L"\n";
					if(nv!=v)
					{
						mod=true;
					}
				}
				else
				{
					new_settings_str+=key+L"="+v+L"\n";
				}
			}
			else
			{
				std::wstring nv;
				if(new_settings->getValue(key, &nv) )
				{
					if(key==L"internet_server" && nv.empty() && !v.empty()
						|| key==L"computername" && nv.empty() && !v.empty())
					{
						new_settings_str+=key+L"="+v+L"\n";
					}
					else
					{
						new_settings_str+=key+L"="+nv+L"\n";
						if(v!=nv)
						{
							mod=true;
						}
					}
				}
				else if(key==L"internet_server" && !v.empty()
					|| key==L"computername" && !v.empty())
				{
					new_settings_str+=key+L"="+v+L"\n";
				}

				if(new_settings->getValue(key+L"_def", &nv) )
				{
					new_settings_str+=key+L"_def="+nv+L"\n";
					if(nv!=def_v)
					{
						mod=true;
					}
				}
			}
		}
	}

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT id FROM backupdirs WHERE server_default=0", false);
	db_results res=q->Read();
	db->destroyQuery(q);
	if(res.empty())
	{
		std::wstring default_dirs;
		if(!new_settings->getValue(L"default_dirs", &default_dirs) )
			new_settings->getValue(L"default_dirs_def", &default_dirs);

		if(!default_dirs.empty())
		{
			std::vector<std::wstring> def_dirs_toks;
			Tokenize(default_dirs, def_dirs_toks, L";");
			str_map args;
			for(size_t i=0;i<def_dirs_toks.size();++i)
			{
				std::wstring path=trim(def_dirs_toks[i]);
				std::wstring name;
				if(path.find(L"|")!=std::string::npos)
				{
					name=trim(getafter(L"|", path));
					path=trim(getuntil(L"|", path));
				}
				args[L"dir_"+convert(i)]=path;
				if(!name.empty())
					args[L"dir_"+convert(i)+L"_name"]=name;
			}

			saveBackupDirs(args, true);
		}
	}

	if(mod)
	{
		writestring(Server->ConvertToUTF8(new_settings_str), "urbackup/data/settings.cfg");

		InternetClient::updateSettings();
	}

	std::auto_ptr<ISettingsReader> curr_server_settings(Server->createFileSettingsReader("urbackup/data/settings_"+server_token+".cfg"));
	std::vector<std::wstring> global_settings = getGlobalizedSettingsList();

	std::wstring new_token_settings=L"";

	bool mod_server_settings=false;
	for(size_t i=0;i<global_settings.size();++i)
	{
		std::wstring key=global_settings[i];

		std::wstring v;
		bool curr_v=curr_server_settings->getValue(key, &v);
		std::wstring nv;
		bool new_v=new_settings->getValue(key, &nv);

		if(!curr_v && new_v)
		{
			new_token_settings+=key+L"="+nv;
			mod_server_settings=true;
		}
		else if(curr_v)
		{
			if(new_v)
			{
				new_token_settings+=key+L"="+nv;

				if(nv!=v)
				{
					mod_server_settings=true;
				}
			}
			else
			{
				new_token_settings+=key+L"="+v;
			}
		}
	}

	if(mod_server_settings)
	{
		writestring(Server->ConvertToUTF8(new_settings_str), "urbackup/data/settings_"+server_token+".cfg");
	}
}

void ClientConnector::replaceSettings(const std::string &pData)
{
	ISettingsReader *new_settings=Server->createMemorySettingsReader(pData);

	std::wstring ncname=new_settings->getValue(L"computername", L"");
	if(!ncname.empty() && ncname!=IndexThread::getFileSrv()->getServerName())
	{
		CWData data;
		data.addChar(7);
		data.addVoidPtr(NULL);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}

	ISettingsReader* old_settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");

	std::vector<std::wstring> new_keys = new_settings->getKeys();
	bool modified_settings=true;
	if(old_settings!=NULL)
	{
		modified_settings=false;
		std::vector<std::wstring> old_keys = old_settings->getKeys();

		for(size_t i=0;i<old_keys.size();++i)
		{
			std::wstring old_val;
			std::wstring new_val;
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
				std::wstring old_val;
				if(!old_settings->getValue(new_keys[i], &old_val))
				{
					modified_settings=true;
					break;
				}
			}
		}

		Server->destroy(old_settings);
	}

	if(modified_settings)
	{
		std::string new_data;

		for(size_t i=0;i<new_keys.size();++i)
		{
			if(new_keys[i]==L"client_set_settings" ||
				new_keys[i]==L"client_set_settings_time")
				continue;

			std::wstring val;
			if(new_settings->getValue(new_keys[i], &val))
			{
				new_data+=Server->ConvertToUTF8(new_keys[i])+"="+Server->ConvertToUTF8(val)+"\n";
			}
		}

		new_data+="client_set_settings=true\n";
		new_data+="client_set_settings_time="+nconvert(Server->getTimeSeconds())+"\n";

		writestring(new_data, "urbackup/data/settings.cfg");
	}

	Server->destroy(new_settings);
}

void ClientConnector::saveLogdata(const std::string &created, const std::string &pData)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q_p=db->Prepare("INSERT INTO logs (ttime) VALUES (datetime(?, 'unixepoch'))");
	q_p->Bind(created);
	q_p->Write();
	_i64 logid=db->getLastInsertID();

	while(!db->Write("BEGIN IMMEDIATE;"))
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
		std::wstring msg=Server->ConvertToUnicode(u_msg);

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
	IQuery *q=db->Prepare("SELECT id, strftime('"+time_format_str+"',ttime, 'localtime') AS ltime FROM logs ORDER BY ttime DESC LIMIT 100");
	db_results res=q->Read(&timeoutms);
	std::string ret;
	for(size_t i=0;i<res.size();++i)
	{
		ret+=Server->ConvertToUTF8(res[i][L"id"])+"-";
		ret+=Server->ConvertToUTF8(res[i][L"ltime"])+"\n";
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
		data+=Server->ConvertToUTF8(res[i][L"loglevel"])+"-";
		data+=Server->ConvertToUTF8(res[i][L"message"])+"\n";
	}
	db->destroyAllQueries();
}

bool ClientConnector::sendFullImage(void)
{
	image_inf.thread_action=TA_FULL_IMAGE;
	image_inf.image_thread=new ImageThread(this, pipe, mempipe, &image_inf, server_token, hashdatafile);
	mempipe=Server->createMemoryPipe();
	mempipe_owner=true;
	image_inf.thread_ticket=Server->getThreadPool()->execute(image_inf.image_thread);
	state=CCSTATE_IMAGE;
	IScopedLock lock(backup_mutex);
	backup_running=RUNNING_FULL_IMAGE;
	pcdone=0;
	backup_source_token=server_token;
	return true;
}

bool ClientConnector::sendIncrImage(void)
{
	image_inf.thread_action=TA_INCR_IMAGE;
	image_inf.image_thread=new ImageThread(this, pipe, mempipe, &image_inf, server_token, hashdatafile);
	mempipe=Server->createMemoryPipe();
	mempipe_owner=true;
	image_inf.thread_ticket=Server->getThreadPool()->execute(image_inf.image_thread);
	state=CCSTATE_IMAGE_HASHDATA;
	IScopedLock lock(backup_mutex);
	backup_running=RUNNING_INCR_IMAGE;
	pcdone=0;
	pcdone2=0;
	backup_source_token=server_token;
	return true;
}

bool ClientConnector::waitForThread(void)
{
	if(image_inf.thread_action!=TA_NONE && Server->getThreadPool()->isRunning(image_inf.thread_ticket ) )
		return true;
	else
		return false;
}

bool ClientConnector::sendMBR(std::wstring dl, std::wstring &errmsg)
{
#ifdef _WIN32
	std::wstring vpath=dl;
	if(!vpath.empty() && vpath[0]!='\\')
	{
		dl+=L":";
		vpath=L"\\\\.\\"+dl;
	}

	HANDLE hVolume=CreateFileW(vpath.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hVolume==INVALID_HANDLE_VALUE)
	{
		errmsg=L"CreateFile of volume '"+dl+L"' failed. - sendMBR. Errorcode: "+convert((int)GetLastError());
		Server->Log(errmsg, LL_ERROR);
		return false;
	}

	STORAGE_DEVICE_NUMBER dev_num;
	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(STORAGE_DEVICE_NUMBER), &ret_bytes, NULL);
	
	if(b==0)
	{
		errmsg=L"DeviceIoControl IOCTL_STORAGE_GET_DEVICE_NUMBER failed. Volume: '"+dl+L"'";
		Server->Log(errmsg, LL_WARNING);

		VOLUME_DISK_EXTENTS *vde=(VOLUME_DISK_EXTENTS*)new char[sizeof(VOLUME_DISK_EXTENTS)];
		b=DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, sizeof(VOLUME_DISK_EXTENTS), &ret_bytes, NULL);
		if(b==0 && GetLastError()==ERROR_MORE_DATA)
		{
			DWORD ext_num=vde->NumberOfDiskExtents;
			errmsg=L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed. Extends: "+convert((int)ext_num);
			Server->Log(errmsg, LL_WARNING);
			delete []vde;
			DWORD vde_size=sizeof(VOLUME_DISK_EXTENTS)+sizeof(DISK_EXTENT)*(ext_num-1);
			vde=(VOLUME_DISK_EXTENTS*)new char[vde_size];
			b=DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, vde_size, &ret_bytes, NULL);
			if(b==0)
			{
				errmsg=L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed twice. Volume: '"+dl+L"'";
				Server->Log(errmsg, LL_ERROR);
				delete []vde;
				CloseHandle(hVolume);
				return false;
			}
		}
		else if(b==0)
		{
			errmsg=L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed. Volume: '"+dl+L"' Error: "+convert((int)GetLastError());
			Server->Log(errmsg, LL_ERROR);
			delete []vde;
			CloseHandle(hVolume);
			return false;
		}

		if(vde->NumberOfDiskExtents>0)
		{
			HANDLE hDevice=CreateFileW((L"\\\\.\\PhysicalDrive"+convert((int)vde->Extents[0].DiskNumber)).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(hDevice==INVALID_HANDLE_VALUE)
			{
				errmsg=L"CreateFile of device '"+dl+L"' failed. - sendMBR";
				Server->Log(errmsg, LL_ERROR);
				delete []vde;
				CloseHandle(hVolume);
				return false;
			}

			DWORD numPartitions=10;
			DWORD inf_size=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1);

			DRIVE_LAYOUT_INFORMATION_EX *inf=(DRIVE_LAYOUT_INFORMATION_EX*)new char[sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1)];

			b=DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, inf, inf_size, &ret_bytes, NULL);
			while(b==0 && GetLastError()==ERROR_INSUFFICIENT_BUFFER && numPartitions<1000)
			{
				numPartitions*=2;
				delete []inf;			
				inf_size=sizeof(DRIVE_LAYOUT_INFORMATION_EX)+sizeof(PARTITION_INFORMATION_EX)*(numPartitions-1);
				inf=(DRIVE_LAYOUT_INFORMATION_EX*)new char[inf_size];
				b=DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, inf, inf_size, &ret_bytes, NULL);
			}
			if(b==0)
			{
				errmsg=L"DeviceIoControl IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed. Volume: '"+dl+L"' Error: "+convert((int)GetLastError());
				Server->Log(errmsg, LL_ERROR);
				delete []vde;
				delete []inf;
				CloseHandle(hDevice);
				CloseHandle(hVolume);
				return false;
			}

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

			delete []vde;
			delete []inf;

			CloseHandle(hDevice);

			if(found)
			{
				errmsg=L"Dynamic volumes are not supported. It may work with mirrored whole disk volumes though. Volume: '"+dl+L"'";
				Server->Log(errmsg, LL_WARNING);
			}
			else
			{
				errmsg=L"Did not find PartitionNumber of dynamic volume. Volume: '"+dl+L"'";
				Server->Log(errmsg, LL_ERROR);
				CloseHandle(hVolume);
				return false;
			}
		}
		else
		{
			errmsg=L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned no extends. Volume: '"+dl+L"'";
			Server->Log(errmsg, LL_ERROR);
			delete []vde;
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
	b=GetVolumeInformationW((dl+L"\\").c_str(), voln, voln_size, &voln_sern, NULL, NULL, fsn, fsn_size);
	if(b==0)
	{
		errmsg=L"GetVolumeInformationW failed. Volume: '"+dl+L"'";
		Server->Log(errmsg, LL_ERROR);
		return false;
	}

	CWData mbr;
	mbr.addChar(1);
	mbr.addChar(0);
	mbr.addInt(dev_num.DeviceNumber);
	mbr.addInt(dev_num.PartitionNumber);
	mbr.addString(nconvert((_i64)voln_sern));
	mbr.addString(Server->ConvertToUTF8((std::wstring)voln));
	mbr.addString(Server->ConvertToUTF8((std::wstring)fsn));

	IFile *dev=Server->openFile(L"\\\\.\\PhysicalDrive"+convert((int)dev_num.DeviceNumber), MODE_READ);

	if(dev==NULL)
	{
		errmsg=L"Error opening Device "+convert((int)dev_num.DeviceNumber);
		Server->Log(errmsg, LL_ERROR);
		return false;
	}

	std::string mbr_bytes=dev->Read(512);

	mbr.addString(mbr_bytes);

	mbr.addString(Server->ConvertToUTF8(errmsg));

	tcpstack.Send(pipe, mbr);

	return true;
#endif //WIN_32
}

const int64 receive_timeouttime=60000;

std::string ClientConnector::receivePacket(IPipe *p)
{
	int64 starttime=Server->getTimeMS();
	while(Server->getTimeMS()-starttime<=receive_timeouttime)
	{
		std::string ret;
		size_t rc=p->Read(&ret, 10000);
		if(rc==0)
		{
			return "";
		}
		tcpstack.AddData((char*)ret.c_str(), ret.size());

		size_t packetsize;
		char *pck=tcpstack.getPacket(&packetsize);
		if(pck!=NULL)
		{
			ret.resize(packetsize);
			if(packetsize>0)
			{
				memcpy(&ret[0], pck, packetsize);
			}
			delete [] pck;
			return ret;
		}
	}
	return "";
}

void ClientConnector::removeChannelpipe(IPipe *cp)
{
	channel_exit.push_back(cp);
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
		IScopedLock lock(progress_mutex);
		pcdone=0;
	}

	for(size_t i=0;i<channel_pipes.size();++i)
	{
		IPipe *c=channel_pipes[i].pipe;
		std::string offset;
		if(params.find(L"offset")!=params.end())
		{
			offset="&offset="+wnarrow(params[L"offset"]);
		}
		tcpstack.Send(c, "DOWNLOAD IMAGE img_id="+wnarrow(params[L"img_id"])+"&time="+wnarrow(params[L"time"])+"&mbr="+wnarrow(params[L"mbr"])+offset);

		Server->Log("Downloading from channel "+nconvert((int)i), LL_DEBUG);

		_i64 imgsize=-1;
		c->Read((char*)&imgsize, sizeof(_i64), 60000);
		Server->Log("Imagesize "+nconvert(imgsize), LL_DEBUG);
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

		if(params[L"mbr"]==L"true")
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

		unsigned int blockleft=0;
		unsigned int off=0;
		_i64 pos=0;
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
				Server->Log("Could not write to pipe! downloadImage-3 size "+nconvert(r)+" off "+nconvert(off), LL_ERROR);
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
						if(s>imgsize)
						{
							Server->Log("invalid seek value: "+nconvert(s), LL_ERROR);
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
					if(off>=r)
					{
						off=0;
						break;
					}
				}
			}

			int t_pcdone=(int)(((float)pos/(float)imgsize)*100.f+0.5f);
			if(t_pcdone!=l_pcdone)
			{
				l_pcdone=t_pcdone;
				IScopedLock lock(progress_mutex);
				pcdone=l_pcdone;
			}

			lasttime=Server->getTimeMS();
		}
		Server->Log("Downloading image done", LL_DEBUG);
		return;
	}
	imgsize=-2;
	pipe->Write((char*)&imgsize, sizeof(_i64), (int)receive_timeouttime);
}

void ClientConnector::waitForPings(IScopedLock *lock)
{
	Server->Log("Waiting for pings...", LL_DEBUG);
	while(!channel_ping.empty())
	{
		lock->relock(NULL);
		Server->wait(10);
		lock->relock(backup_mutex);
	}
	Server->Log("done. (Waiting for pings)", LL_DEBUG);
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
	if(backup_running!=RUNNING_NONE && Server->getTimeMS()-last_pingtime<x_pingtimeout)
		tcpstack.Send(pipe, "RUNNING");
	else
	{
		bool ok=false;
		if(channel_pipe.pipe!=NULL)
		{
			CTCPStack tmpstack(channel_pipe.internet_connection);
			_u32 rc=(_u32)tmpstack.Send(channel_pipe.pipe, ts);
			if(rc!=0)
				ok=true;
		}
		if(!ok)
		{
			tcpstack.Send(pipe, "FAILED");
		}
		else
		{
			tcpstack.Send(pipe, "OK");
		}
	}
}

void ClientConnector::doQuitClient(void)
{
	do_quit=true;
}

void ClientConnector::updatePCDone2(int nv)
{
	IScopedLock lock(backup_mutex);
	pcdone2=nv;
}

bool ClientConnector::isQuitting(void)
{
	return do_quit;
}

bool ClientConnector::isHashdataOkay(void)
{
	return hashdataok;
}

void ClientConnector::resetImageBackupStatus(void)
{
	IScopedLock lock(backup_mutex);
	if(backup_running==RUNNING_FULL_IMAGE || backup_running==RUNNING_INCR_IMAGE)
	{
		backup_running=RUNNING_NONE;
	}
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
#ifdef _WIN32
	Server->getThreadPool()->execute(new UpdateSilentThread());
#endif
}

bool ClientConnector::calculateFilehashesOnClient(void)
{
	if(internet_conn)
	{
		ISettingsReader *curr_settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");

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

	std::string job = getCurrRunningJob();

	return job!="NOA" && job!="DONE";
}