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

#include "ClientService.h"
#include "client.h"
#include "../Interface/Server.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/File.h"
#include "../Interface/ThreadPool.h"
#include "../stringtools.h"
#include "escape.h"
#include "database.h"
#include "fileclient/data.h"
#include "../fsimageplugin/IFSImageFactory.h"
#include "../fsimageplugin/IFilesystem.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "sha2/sha2.h"
#include "ClientSend.h"
#include "ServerIdentityMgr.h"
#include "settings.h"
#include "capa_bits.h"
#ifdef _WIN32
#include "win_sysvol.h"
#else
std::wstring getSysVolume(std::wstring &mpath){ return L""; }
#endif

#include <memory.h>
#include <stdlib.h>
#include <limits.h>

#ifndef _WIN32
#define _atoi64 atoll
#endif

extern IFSImageFactory *image_fak;
extern ICryptoFactory *crypto_fak;
extern unsigned char *zero_hash;

ICustomClient* ClientService::createClient()
{
	return new ClientConnector();
}

void ClientService::destroyClient( ICustomClient * pClient)
{
	delete ((ClientConnector*)pClient);
}

int ClientConnector::backup_running=0;
volatile bool ClientConnector::backup_done=false;
IMutex *ClientConnector::backup_mutex=NULL;
unsigned int ClientConnector::incr_update_intervall=0;
unsigned int ClientConnector::last_pingtime=0;
IPipe *ClientConnector::channel_pipe=NULL;
int ClientConnector::pcdone=0;
int ClientConnector::pcdone2=0;
std::vector<IPipe*> ClientConnector::channel_pipes;
std::vector<IPipe*> ClientConnector::channel_exit;
std::vector<IPipe*> ClientConnector::channel_ping;
std::vector<int> ClientConnector::channel_capa;
IMutex *ClientConnector::progress_mutex=NULL;
volatile bool ClientConnector::img_download_running=false;
db_results ClientConnector::cached_status;
std::string ClientConnector::backup_source_token;
std::map<std::string, unsigned int> ClientConnector::last_token_times;
int ClientConnector::last_capa=0;

const unsigned int x_pingtimeout=180000;

#ifdef _WIN32
const std::string pw_file="pw.txt";
#else
const std::string pw_file="urbackup/pw.txt";
#endif

void ClientConnector::init_mutex(void)
{
	if(backup_mutex==NULL)
	{
		backup_mutex=Server->createMutex();
		progress_mutex=Server->createMutex();
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

bool ClientConnector::wantReceive(void)
{
	return want_receive;
}

void ClientConnector::Init(THREAD_ID pTID, IPipe *pPipe)
{
	tid=pTID;
	pipe=pPipe;
	state=0;
	thread_action=0;
	mempipe=Server->createMemoryPipe();
	lasttime=Server->getTimeMS();
	do_quit=false;
	is_channel=false;
	want_receive=true;
	last_channel_ping=0;
	file_version=1;
}

ClientConnector::~ClientConnector(void)
{
	mempipe->Write("exit");
}

bool ClientConnector::Run(void)
{
	if(do_quit)
	{
		if(is_channel)
		{
			IScopedLock lock(backup_mutex);
			if(channel_pipe==pipe)
			{
				channel_pipe=NULL;
			}
			for(size_t i=0;i<channel_pipes.size();++i)
			{
				if(channel_pipes[i]==pipe)
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
		return false;
	}

	switch(state)
	{
	case 0:
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
	case 1:
		{
			std::string msg;
			mempipe->Read(&msg, 0);
			if(msg=="exit")
			{
				mempipe->Write(msg);
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				IScopedLock lock(backup_mutex);
				backup_running=0;
				backup_done=true;
				return false;
			}
			else if(msg=="done")
			{
				tcpstack.Send(pipe, "DONE");
				lasttime=Server->getTimeMS();
				state=0;
			}
			else if(!msg.empty())
			{
				tcpstack.Send(pipe, msg);
				lasttime=Server->getTimeMS();
				state=0;
				IScopedLock lock(backup_mutex);
				backup_running=0;
				backup_done=true;
			}
			else if(file_version>1 && Server->getTimeMS()-last_update_time>30000)
			{
				last_update_time=Server->getTimeMS();
				tcpstack.Send(pipe, "BUSY");
			}
		}break;
	case 2:
		{
			std::string msg;
			mempipe->Read(&msg, 0);
			if(msg=="exit")
			{
				mempipe->Write(msg);
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				return false;
			}
			else if(msg.find("done")==0)
			{
				tcpstack.Send(pipe, "DONE");
				lasttime=Server->getTimeMS();
				state=0;
			}
			else if(msg.find("failed")==0)
			{
				tcpstack.Send(pipe, "FAILED");
				lasttime=Server->getTimeMS();
				state=0;
			}
		}break;
	case 3: //Channel
		{		
			IScopedLock lock(backup_mutex);
			if(Server->getTimeMS()-lasttime>180000)
			{
				Server->Log("Client timeout in ClientConnector::Run - Channel", LL_DEBUG);
				{
					if(channel_pipe==pipe)
						channel_pipe=NULL;
					for(size_t i=0;i<channel_pipes.size();++i)
					{
						if(channel_pipes[i]==pipe)
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
			/*if(channel_pipe!=pipe)
			{
				Server->Log("Channel got replaced.", LL_DEBUG);
				if(waitForThread())
				{
					do_quit=true;
					return true;
				}
				return false;
			}*/
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
	case 4:
	case 5:
		{
			if(Server->getThreadPool()->isRunning(thread_ticket)==false )
			{
				return false;
			}
		}break;
	case 6:
	case 7:
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

			if(state==7)
			{
				if(hashdataok)
				{
					hashdatafile->Seek(0);
					writeUpdateFile(hashdatafile, "version_new.txt");
					writeUpdateFile(hashdatafile, "UrBackupUpdate.sig");
					writeUpdateFile(hashdatafile, "UrBackupUpdate_untested.exe");

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
									Server->deleteFile("version.txt");
									Server->deleteFile("UrBackupUpdate.exe");
									moveFile(L"UrBackupUpdate_untested.exe", L"UrBackupUpdate.exe");
									moveFile(L"version_new.txt", L"version.txt");
									tcpstack.Send(pipe, "ok");
								}
								else
								{									
									Server->Log("Verifying update file failed. Signature did not match", LL_ERROR);
									tcpstack.Send(pipe, "verify_sig_err");
								}
							}
							else
							{
								Server->destroy(updatefile);
								Server->Log("Verifing update file failed. Update was installed previously", LL_ERROR);
								tcpstack.Send(pipe, "verify_sig_already_used_err");
							}
						}
					}
					else
					{
						Server->Log("Verifing update file failed. Cryptomodule not present", LL_ERROR);
						tcpstack.Send(pipe, "verify_cryptmodule_err");
					}
				}
				else
				{
					do_quit=true;
				}
			}
			return true;
		}break;
	case 8: // wait for contractors
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
	if(state==8)
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
		Server->Log("rc=0 hasError="+nconvert(pipe->hasError())+" state="+nconvert(state), LL_DEBUG);
#ifdef _WIN32
#ifdef _DEBUG
		Server->Log("Err: "+nconvert((int)GetLastError()), LL_DEBUG);
#endif
#endif
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
	if(state==5 || state==6)
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
			Server->Log("Hashdataleft: "+nconvert(hashdataleft), LL_DEBUG);
		}
		else
		{
			Server->Log("Too much hashdata - error", LL_ERROR);
		}

		if(hashdataleft==0)
		{
			hashdataok=true;
			if(state==5)
				state=4;
			else if(state==6)
				state=7;
			return;
		}
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
		std::string identity;
		bool ident_ok=false;
		str_map params;
		size_t hashpos;
		if(cmd.size()>3 && cmd[0]=='#' && cmd[1]=='I' )
		{
			identity=getbetween("#I", "#", cmd);
			cmd.erase(0, identity.size()+3);
			size_t tp=cmd.find("#token=");
			if(tp!=std::string::npos)
			{
				server_token=cmd.substr(tp+7);
				cmd.erase(tp, cmd.size()-tp);
			}
		}
		else if((hashpos=cmd.find("#"))!=std::string::npos)
		{
			ParseParamStr(getafter("#", cmd), &params);

			cmd.erase(hashpos, cmd.size()-hashpos);
			if(!checkPassword(params[L"pw"]))
			{
				Server->Log("Password wrong!", LL_ERROR);
				break;
			}
			else
			{
				pw_ok=true;
			}
		}

		if(!identity.empty() && ServerIdentityMgr::checkServerIdentity(identity)==true)
		{
			ident_ok=true;
		}

		if(cmd=="ADD IDENTITY")
		{
			if(identity.empty())
			{
				tcpstack.Send(pipe, "Identity empty");
			}
			else
			{
				if(Server->getServerParameter("restore_mode")=="true" && !ident_ok )
				{
					ServerIdentityMgr::addServerIdentity(identity);
						tcpstack.Send(pipe, "OK");
				}
				else if( ident_ok )
				{
					tcpstack.Send(pipe, "OK");
				}
				else
				{
					if( ServerIdentityMgr::numServerIdentities()==0 )
					{
						ServerIdentityMgr::addServerIdentity(identity);
						tcpstack.Send(pipe, "OK");
					}
					else
					{
						tcpstack.Send(pipe, "failed");
					}
				}
			}
		}
		else if( (cmd=="START BACKUP" || cmd=="2START BACKUP") && ident_ok==true)
		{
			if(cmd=="2START BACKUP") file_version=2;

			state=1;

			CWData data;
			data.addChar(0);
			data.addVoidPtr(mempipe);
			data.addString(server_token);
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

			lasttime=Server->getTimeMS();

			IScopedLock lock(backup_mutex);
			backup_running=1;
			last_pingtime=Server->getTimeMS();
			pcdone=0;
			backup_source_token=server_token;
		}
		else if( (cmd=="START FULL BACKUP" || cmd=="2START FULL BACKUP") && ident_ok==true)
		{
			if(cmd=="2START FULL BACKUP") file_version=2;

			state=1;

			CWData data;
			data.addChar(1);
			data.addVoidPtr(mempipe);
			data.addString(server_token);
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

			lasttime=Server->getTimeMS();

			IScopedLock lock(backup_mutex);
			backup_running=2;
			last_pingtime=Server->getTimeMS();
			pcdone=0;
			backup_source_token=server_token;
		}
		else if(cmd.find("START SC \"")!=std::string::npos && ident_ok==true)
		{
#ifdef _WIN32
			if(cmd[cmd.size()-1]=='"')
			{
				state=2;
				std::string dir=cmd.substr(10, cmd.size()-11);
				CWData data;
				data.addChar(2);
				data.addVoidPtr(mempipe);
				data.addString(dir);
				data.addString(server_token);
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
				lasttime=Server->getTimeMS();
			}
			else
			{
				Server->Log("Invalid command", LL_ERROR);
			}
#else
			tcpstack.Send(pipe, "DONE");
#endif
		}
		else if(cmd.find("STOP SC \"")!=std::string::npos && ident_ok==true)
		{
#ifdef _WIN32
			if(cmd[cmd.size()-1]=='"')
			{
				state=2;
				std::string dir=cmd.substr(9, cmd.size()-10);
				CWData data;
				data.addChar(3);
				data.addVoidPtr(mempipe);
				data.addString(dir);
				data.addString(server_token);
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
				lasttime=Server->getTimeMS();
			}
			else
			{
				Server->Log("Invalid command", LL_ERROR);
			}
#else
			tcpstack.Send(pipe, "DONE");
#endif
		}
		else if(cmd.find("INCRINTERVALL \"")!=std::string::npos && ident_ok==true)
		{
			if(cmd[cmd.size()-1]=='"')
			{
				std::string intervall=cmd.substr(15, cmd.size()-16);
				incr_update_intervall=atoi(intervall.c_str());
				tcpstack.Send(pipe, "OK");
				lasttime=Server->getTimeMS();
			}
			else
			{
				Server->Log("Invalid command", LL_ERROR);
			}			
		}
		else if(cmd.find("1GET BACKUP DIRS")==0 && pw_ok==true)
		{
			getBackupDirs(1);
			lasttime=Server->getTimeMS();
		}
		else if(cmd.find("GET BACKUP DIRS")==0 && pw_ok==true)
		{
			getBackupDirs();
			lasttime=Server->getTimeMS();
		}
		else if(cmd.find("SAVE BACKUP DIRS")==0 && pw_ok==true)
		{
			if(saveBackupDirs(params))
			{
				tcpstack.Send(pipe, "OK");
			}
			lasttime=Server->getTimeMS();
		}
		else if(cmd.find("GET INCRINTERVALL")==0 && pw_ok==true)
		{
			if(incr_update_intervall==0 )
			{
				tcpstack.Send(pipe, nconvert(0));
			}
			else
			{
				tcpstack.Send(pipe, nconvert(incr_update_intervall+10*60) );
			}
			lasttime=Server->getTimeMS();
		}
		else if(cmd=="DID BACKUP" && ident_ok==true)
		{
			updateLastBackup();
			tcpstack.Send(pipe, "OK");

			{
				IScopedLock lock(backup_mutex);
				if(backup_running==1 || backup_running==2)
				{
					backup_running=0;
					backup_done=true;
				}
				lasttime=Server->getTimeMS();
			}
			
			IndexThread::execute_postbackup_hook();
		}
		else if(cmd.find("STATUS")==0 && pw_ok==true)
		{
			getBackupStatus();			
			lasttime=Server->getTimeMS();
		}
		else if(cmd.find("SETTINGS ")==0 && ident_ok==true)
		{
			std::string s_settings=cmd.substr(9);
			unescapeMessage(s_settings);
			updateSettings( s_settings );
			tcpstack.Send(pipe, "OK");
			lasttime=Server->getTimeMS();
		}
		else if(cmd.find("PING RUNNING")==0 && ident_ok==true)
		{
			tcpstack.Send(pipe, "OK");
			IScopedLock lock(backup_mutex);
			lasttime=Server->getTimeMS();
			last_pingtime=Server->getTimeMS();
			int pcdone_new=atoi(getbetween("-","-", cmd).c_str());
			if(backup_source_token.empty() || backup_source_token==server_token )
			{
				pcdone=pcdone_new;
			}
			last_token_times[server_token]=Server->getTimeSeconds();

#ifdef _WIN32
			SetThreadExecutionState(ES_SYSTEM_REQUIRED);
#endif
		}
		else if( (cmd=="CHANNEL" || cmd.find("1CHANNEL")==0 ) && ident_ok==true)
		{
			if(!img_download_running)
			{
				g_lock.relock(backup_mutex);
				channel_pipe=pipe;
				channel_pipes.push_back(pipe);
				is_channel=true;
				state=3;
				last_channel_ping=Server->getTimeMS();
				lasttime=Server->getTimeMS();
				Server->Log("New channel: Number of Channels: "+nconvert((int)channel_pipes.size()), LL_DEBUG);

				int capa=0;
				if(cmd.find("1CHANNEL ")==0)
				{
					std::string s_params=cmd.substr(9);
					str_map params;
					ParseParamStr(s_params, &params);
					capa=watoi(params[L"capa"]);
				}
				channel_capa.push_back(capa);
			}
		}
		else if(cmd=="PONG" && is_channel==true )
		{
			lasttime=Server->getTimeMS();
			IScopedLock lock(backup_mutex);
			for(size_t i=0;i<channel_ping.size();++i)
			{
				if(channel_ping[i]==pipe)
				{
					channel_ping.erase(channel_ping.begin()+i);
					break;
				}
			}
		}
		else if(cmd=="PING" && is_channel==true )
		{
			lasttime=Server->getTimeMS();
			if(tcpstack.Send(pipe, "PONG")==0)
			{
				do_quit=true;
			}
		}
		else if(cmd.find("START BACKUP INCR")==0 && pw_ok==true )
		{
			IScopedLock lock(backup_mutex);
			lasttime=Server->getTimeMS();
			if(backup_running!=0 && Server->getTimeMS()-last_pingtime<x_pingtimeout)
				tcpstack.Send(pipe, "RUNNING");
			else
			{
				bool ok=false;
				if(channel_pipe!=NULL)
				{
					_u32 rc=(_u32)tcpstack.Send(channel_pipe, "START BACKUP INCR");
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
		else if(cmd.find("START BACKUP FULL")==0 && pw_ok==true )
		{
			IScopedLock lock(backup_mutex);
			lasttime=Server->getTimeMS();
			if(backup_running!=0 && Server->getTimeMS()-last_pingtime<x_pingtimeout)
				tcpstack.Send(pipe, "RUNNING");
			else
			{
				bool ok=false;
				if(channel_pipe!=NULL)
				{
					_u32 rc=(_u32)tcpstack.Send(channel_pipe, "START BACKUP FULL");
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
		else if(cmd.find("START IMAGE FULL")==0 && pw_ok==true )
		{
			IScopedLock lock(backup_mutex);
			lasttime=Server->getTimeMS();
			if(backup_running!=0 && Server->getTimeMS()-last_pingtime<x_pingtimeout )
				tcpstack.Send(pipe, "RUNNING");
			else
			{
				bool ok=false;
				if(channel_pipe!=NULL)
				{
					_u32 rc=(_u32)tcpstack.Send(channel_pipe, "START IMAGE FULL");
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
		else if(cmd.find("START IMAGE INCR")==0 && pw_ok==true )
		{
			IScopedLock lock(backup_mutex);
			lasttime=Server->getTimeMS();
			if(backup_running!=0 && Server->getTimeMS()-last_pingtime<x_pingtimeout)
				tcpstack.Send(pipe, "RUNNING");
			else
			{
				bool ok=false;
				if(channel_pipe!=NULL)
				{
					_u32 rc=(_u32)tcpstack.Send(channel_pipe, "START IMAGE INCR");
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
		else if(cmd.find("UPDATE SETTINGS ")==0 && pw_ok==true )
		{
			std::string s_settings=cmd.substr(16);
			lasttime=Server->getTimeMS();
			unescapeMessage(s_settings);
			replaceSettings( s_settings );

			IScopedLock lock(backup_mutex);
			bool ok=false;
			for(size_t o=0;o<channel_pipes.size();++o)
			{
				_u32 rc=(_u32)tcpstack.Send(channel_pipes[o], "UPDATE SETTINGS");
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
		else if(cmd.find("2LOGDATA ")==0 && ident_ok==true)
		{
			std::string ldata=cmd.substr(9);
			size_t cpos=ldata.find(" ");
			std::string created=getuntil(" ", ldata);
			lasttime=Server->getTimeMS();
			saveLogdata(created, getafter(" ",ldata));
			tcpstack.Send(pipe, "OK");
		}
		else if(cmd.find("PAUSE ")==0 && pw_ok==true)
		{
			lasttime=Server->getTimeMS();
			std::string b=cmd.substr(6);
			bool ok=false;
			if(b=="true")
			{
				ok=true;
				IdleCheckerThread::setPause(true);
				IndexThread::getFileSrv()->setPause(true);
			}
			else if(b=="false")
			{
				ok=true;
				IdleCheckerThread::setPause(false);
				IndexThread::getFileSrv()->setPause(false);
			}
			if(ok) tcpstack.Send(pipe, "OK");
			else   tcpstack.Send(pipe, "FAILED");
		}
		else if(cmd.find("GET LOGPOINTS")==0 && pw_ok==true)
		{
			lasttime=Server->getTimeMS();
			tcpstack.Send(pipe, getLogpoints() );
		}
		else if(cmd.find("GET LOGDATA")==0 && pw_ok==true )
		{
			lasttime=Server->getTimeMS();
			int logid=watoi(params[L"logid"]);
			int loglevel=watoi(params[L"loglevel"]);
			std::string ret;
			getLogLevel(logid, loglevel, ret);
			tcpstack.Send(pipe, ret);
		}
		else if(cmd.find("FULL IMAGE ")==0)
		{
			if(ident_ok==true)
			{
				lasttime=Server->getTimeMS();
				std::string s_params=cmd.substr(11);
				str_map params;
				ParseParamStr(s_params, &params);

				server_token=Server->ConvertToUTF8(params[L"token"]);
				image_letter=Server->ConvertToUTF8(params[L"letter"]);
				shadowdrive=Server->ConvertToUTF8(params[L"shadowdrive"]);
				if(params.find(L"start")!=params.end())
				{
					startpos=(uint64)_atoi64(Server->ConvertToUTF8(params[L"start"]).c_str());
				}
				else
				{
					startpos=0;
				}
				if(params.find(L"shadowid")!=params.end())
				{
					shadow_id=watoi(params[L"shadowid"]);
				}
				else
				{
					shadow_id=-1;
				}

				no_shadowcopy=false;

				if(image_letter=="SYSVOL")
				{
					std::wstring mpath;
					std::wstring sysvol=getSysVolume(mpath);
					if(!mpath.empty())
					{
						image_letter=Server->ConvertToUTF8(mpath);
					}
					else
					{
						image_letter=Server->ConvertToUTF8(sysvol);
						no_shadowcopy=true;
					}
				}

				if(startpos==0 && !no_shadowcopy)
				{
					CWData data;
					data.addChar(2);
					data.addVoidPtr(mempipe);
					data.addString(image_letter);
					data.addString(server_token);
					data.addUChar(1); //image backup
					data.addUChar(0); //filesrv
					IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
				}
				else if(shadow_id!=-1)
				{
					shadowdrive.clear();
					CWData data;
					data.addChar(4);
					data.addVoidPtr(mempipe);
					data.addInt(shadow_id);
					IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
				}

				if(no_shadowcopy)
				{
					shadowdrive=image_letter;
					if(!shadowdrive.empty() && shadowdrive[0]!='\\')
					{
						shadowdrive="\\\\.\\"+image_letter;
					}
				}

				lasttime=Server->getTimeMS();
				sendFullImage();
			}
			else
			{
				ImageErr("Ident reset (1)");
			}
		}
		else if(cmd.find("INCR IMAGE ")==0  && ident_ok==true)
		{
			if(ident_ok==true)
			{
				lasttime=Server->getTimeMS();
				std::string s_params=cmd.substr(11);
				str_map params;
				ParseParamStr(s_params, &params);

				server_token=Server->ConvertToUTF8(params[L"token"]);

				str_map::iterator f_hashsize=params.find(L"hashsize");
				if(f_hashsize!=params.end())
				{
					hashdataok=false;
					hashdataleft=watoi(f_hashsize->second);
					image_letter=Server->ConvertToUTF8(params[L"letter"]);
					shadowdrive=Server->ConvertToUTF8(params[L"shadowdrive"]);
					if(params.find(L"start")!=params.end())
					{
						startpos=(uint64)_atoi64(Server->ConvertToUTF8(params[L"start"]).c_str());
					}
					else
					{
						startpos=0;
					}
					if(params.find(L"shadowid")!=params.end())
					{
						shadow_id=watoi(params[L"shadowid"]);
					}
					else
					{
						shadow_id=-1;
					}

					no_shadowcopy=false;

					if(startpos==0)
					{
						CWData data;
						data.addChar(2);
						data.addVoidPtr(mempipe);
						data.addString(image_letter);
						data.addString(server_token);
						data.addUChar(1); //image backup
						data.addUChar(0); //file serv?
						IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
					}
					else if(shadow_id!=-1)
					{
						shadowdrive.clear();
						CWData data;
						data.addChar(4);
						data.addVoidPtr(mempipe);
						data.addInt(shadow_id);
						IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
					}
					hashdatafile=Server->openTemporaryFile();
					if(hashdatafile==NULL)
						continue;

					if(tcpstack.getBuffersize()>0)
					{
						if(hashdatafile->Write(tcpstack.getBuffer(), (_u32)tcpstack.getBuffersize())!=tcpstack.getBuffersize())
						{
							Server->Log("Error writing to hashdata temporary file -1", LL_ERROR);
							do_quit=true;
							return;
						}
						if(hashdataleft>=tcpstack.getBuffersize())
						{
							hashdataleft-=(_u32)tcpstack.getBuffersize();
							//Server->Log("Hashdataleft: "+nconvert(hashdataleft), LL_DEBUG);
						}
						else
						{
							Server->Log("Too much hashdata - error -1", LL_ERROR);
						}

						if(hashdataleft==0)
						{
							hashdataok=true;
							state=4;
							return;
						}
					}

					lasttime=Server->getTimeMS();
					sendIncrImage();
				}
				else
				{
					ImageErr("Ident reset (2)");
				}
			}
		}
		else if( cmd.find("MBR ")==0 && ident_ok==true )
		{
			lasttime=Server->getTimeMS();
			std::string s_params=cmd.substr(4);
			str_map params;
			ParseParamStr(s_params, &params);

			std::wstring dl=params[L"driveletter"];

			if(dl==L"SYSVOL")
			{
				std::wstring mpath;
				dl=getSysVolume(mpath);
			}

			bool b=false;
			if(!dl.empty())
			{
				b=sendMBR(dl);
			}
			if(!b)
			{
				CWData r;r.addChar(0);
				tcpstack.Send(pipe, r);
			}
		}
		else if( cmd.find("GET BACKUPCLIENTS")==0 && pw_ok==true )
		{
			lasttime=Server->getTimeMS();
			IScopedLock lock(backup_mutex);
			waitForPings(&lock);
			if(channel_pipes.size()==0)
			{
				tcpstack.Send(pipe, "0");
			}
			else
			{
				std::string clients;
				for(size_t i=0;i<channel_pipes.size();++i)
				{
					tcpstack.Send(channel_pipes[i], "GET BACKUPCLIENTS");
					if(channel_pipes[i]->hasError())
						Server->Log("Channel has error after request -1", LL_DEBUG);
					std::string nc=receivePacket(channel_pipes[i]);
					if(channel_pipes[i]->hasError())
						Server->Log("Channel has error after read -1", LL_DEBUG);
						
					Server->Log("Client "+nconvert(i)+"/"+nconvert(channel_pipes.size())+": --"+nc+"--", LL_DEBUG);
					
					if(!nc.empty())
					{
						clients+=nc+"\n";
					}
				}
				tcpstack.Send(pipe, "1"+clients);
			}
		}
		else if( cmd.find("GET BACKUPIMAGES ")==0 && pw_ok==true )
		{
			lasttime=Server->getTimeMS();
			IScopedLock lock(backup_mutex);
			waitForPings(&lock);
			if(channel_pipes.size()==0)
			{
				tcpstack.Send(pipe, "0");
			}
			else
			{
				std::string imgs;
				for(size_t i=0;i<channel_pipes.size();++i)
				{
					tcpstack.Send(channel_pipes[i], cmd);
					std::string nc=receivePacket(channel_pipes[i]);
					if(!nc.empty())
					{
						imgs+=nc+"\n";
					}
				}
				tcpstack.Send(pipe, "1"+imgs);
			}
		}
		else if( cmd.find("DOWNLOAD IMAGE")==0 && pw_ok==true )
		{
			lasttime=Server->getTimeMS();
			Server->Log("Downloading image...", LL_DEBUG);
			IScopedLock lock(backup_mutex);
			waitForPings(&lock);
			Server->Log("In mutex...",LL_DEBUG);
			img_download_running=true;
			downloadImage(params);
			img_download_running=false;
			Server->Log("Donwload done -2", LL_DEBUG);
			do_quit=true;
		}
		else if( cmd.find("GET DOWNLOADPROGRESS")==0 && pw_ok==true)
		{
			Server->Log("Sending progress...", LL_DEBUG);
			lasttime=Server->getTimeMS();
			if(img_download_running==false)
			{
				pipe->Write("100");
				do_quit=true;
			}
			else
			{
				while(img_download_running)
				{
					{
						int progress=0;
						{
							IScopedLock lock(progress_mutex);
							progress=pcdone;
						}
						if(!pipe->Write(nconvert(progress)+"\n", 10000))
						{
							break;
						}
					}
					{
						Server->wait(1000);
					}
				}
				do_quit=true;
			}
		}
		else if( cmd.find("VERSION ")==0 && ident_ok==true )
		{
			int n_version=atoi(cmd.substr(8).c_str());
			std::string version_1=getFile("version.txt");
			std::string version_2=getFile("curr_version.txt");
			if(version_1.empty() ) version_1="0";
			if(version_2.empty() ) version_2="0";

			#ifdef _WIN32
			if( atoi(version_1.c_str())<n_version && atoi(version_2.c_str())<n_version )
			{
				tcpstack.Send(pipe, "update");
			}
			else
			{
			#endif
				tcpstack.Send(pipe, "noop");
			#ifdef _WIN32
			}
			#endif
		}
		else if( cmd.find("CLIENTUPDATE ")==0 && ident_ok==true )
		{
			hashdatafile=Server->openTemporaryFile();
			if(hashdatafile==NULL)
				continue;

			hashdataleft=atoi(cmd.substr(13).c_str());
			hashdataok=false;
			state=6;

			if(tcpstack.getBuffersize()>0)
			{
				if(hashdatafile->Write(tcpstack.getBuffer(), (_u32)tcpstack.getBuffersize())!=tcpstack.getBuffersize())
				{
					Server->Log("Error writing to hashdata temporary file -1update", LL_ERROR);
					do_quit=true;
					return;
				}
				if(hashdataleft>=tcpstack.getBuffersize())
				{
					hashdataleft-=(_u32)tcpstack.getBuffersize();
					//Server->Log("Hashdataleft: "+nconvert(hashdataleft), LL_DEBUG);
				}
				else
				{
					Server->Log("Too much hashdata - error -1update", LL_ERROR);
				}

				if(hashdataleft==0)
				{
					hashdataok=true;
					state=6;
					return;
				}
			}
			return;
		}
		else if(cmd.find("CAPA")==0  && ident_ok==true)
		{
#ifdef _WIN32
			tcpstack.Send(pipe, "FILE=2&IMAGE=1&UPDATE=1&MBR=1&FILESRV=1&SET_SETTINGS=1");
#else
			tcpstack.Send(pipe, "FILE=2&FILESRV=1&SET_SETTINGS=1");
#endif
		}
		else
		{
			tcpstack.Send(pipe, "ERR");
		}
	}
}

bool ClientConnector::checkPassword(const std::wstring &pw)
{
	static std::string stored_pw=getFile(pw_file);
	return stored_pw==Server->ConvertToUTF8(pw);
}

void ClientConnector::getBackupDirs(int version)
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT id,name,path FROM backupdirs");
	int timeoutms=300;
	db_results res=q->Read(&timeoutms);
	if(timeoutms==0)
	{
		std::string msg;
		for(size_t i=0;i<res.size();++i)
		{
			if(res[i][L"name"]==L"*") continue;

			msg+=Server->ConvertToUTF8(res[i][L"id"])+"\n";
			if(version!=0)
			{
				msg+=Server->ConvertToUTF8(res[i][L"name"])+"|";
			}
			msg+=Server->ConvertToUTF8(res[i][L"path"]);
			if(i+1<res.size())
				msg+="\n";
		}
		tcpstack.Send(pipe, msg);
	}
	else
	{
		pipe->shutdown();
	}	
	db->destroyAllQueries();
}

std::wstring removeChars(std::wstring in)
{
	wchar_t illegalchars[] = {'*', ':', '/' , '\\'};
	std::wstring ret;
	for(size_t i=0;i<in.size();++i)
	{
		bool found=false;
		for(size_t j=0;j<sizeof(illegalchars);++j)
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
	IQuery *q=db->Prepare("INSERT INTO backupdirs (name, path, server_default) VALUES (?, ? ,"+nconvert(server_default?1:0)+")");
	if(server_default==false)
	{
		q->Bind(L"*"); q->Bind(L"*");;
		q->Write();
		q->Reset();
	}
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
		state=8;
		want_receive=false;
	}

	for(size_t i=0;i<backupdirs.size();++i)
	{
		if(backupdirs[i][L"need"]!=L"1")
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
			state=8;
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
		if( !isletter(in[i]) && !isnumber(in[i]) && !found )
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

void ClientConnector::getBackupStatus(void)
{
	IScopedLock lock(backup_mutex);

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT strftime('"+time_format_str+"',last_backup, 'localtime') AS last_backup FROM status");
	if(q==NULL)
		return;
	int timeoutms=300;
	db_results res=q->Read(&timeoutms);
	if(timeoutms==1)
	{
		res=cached_status;
	}
	else
	{
		cached_status=res;
	}

	std::string ret;
	if(res.size()>0)
	{
		ret+=Server->ConvertToUTF8(res[0][L"last_backup"]);
	}

	if(backup_running==0)
	{
		if(backup_done)
			ret+="#DONE";
		else
			ret+="#NOA";

		backup_done=false;
	}
	else if(backup_running==1)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#INCR";
	}
	else if(backup_running==2)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#FULL";
	}
	else if(backup_running==3)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#FULLI";
	}
	else if(backup_running==4)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#INCRI";
	}

	if(backup_running!=4)
		ret+="#"+nconvert(pcdone);
	else
		ret+="#"+nconvert(pcdone2);

	if(IdleCheckerThread::getPause())
	{
		ret+="#P";
	}
	else
	{
		ret+="#NP";
	}

	int capa=0;
	if(channel_capa.size()==0)
	{
		capa=last_capa;
		capa|=DONT_ALLOW_STARTING_FILE_BACKUPS;
		capa|=DONT_ALLOW_STARTING_IMAGE_BACKUPS;
	}
	else
	{
		capa=INT_MAX;
		for(size_t i=0;i<channel_capa.size();++i)
		{
			capa=capa & channel_capa[i];
		}
		
		if(capa!=last_capa)
		{
			IQuery *cq=db->Prepare("UPDATE misc SET tvalue=? WHERE tkey='last_capa'");
			if(cq!=NULL)
			{
				cq->Bind(capa);
				cq->Write();
				cq->Reset();
				last_capa=capa;
			}
		}
	}

	ret+="#capa="+nconvert(capa);

	tcpstack.Send(pipe, ret);

	db->destroyAllQueries();
}

std::vector<std::wstring> getSettingsList(void);

void ClientConnector::updateSettings(const std::string &pData)
{
	ISettingsReader *curr_settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");
	ISettingsReader *new_settings=Server->createMemorySettingsReader(pData);

	std::vector<std::wstring> settings_names=getSettingsList();
	std::wstring new_settings_str=L"";
	bool mod=false;
	std::string tmp_str;
	bool client_set_settings=false;
	if(curr_settings->getValue("client_set_settings", &tmp_str) && tmp_str=="true")
	{
		client_set_settings=true;
	}
	for(size_t i=0;i<settings_names.size();++i)
	{
		std::wstring key=settings_names[i];
		std::wstring v;
		if(!curr_settings->getValue(key, &v) )
		{
			std::wstring nv;
			if(new_settings->getValue(key, &nv) )
			{				
				new_settings_str+=key+L"="+nv+L"\n";
				mod=true;
			}
			else if(new_settings->getValue(key+L"_def", &nv) )
			{
				new_settings_str+=key+L"_def="+nv+L"\n";
				mod=true;
			}
		}
		else
		{
			if(client_set_settings)
			{
				new_settings_str+=key+L"="+v+L"\n";
			}
			else
			{
				std::wstring nv;
				if(new_settings->getValue(key, &nv) )
				{				
					new_settings_str+=key+L"="+nv+L"\n";
					mod=true;
				}
				else if(new_settings->getValue(key+L"_def", &nv) )
				{
					new_settings_str+=key+L"_def="+nv+L"\n";
					mod=true;
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

	Server->destroy(curr_settings);
	Server->destroy(new_settings);

	if(mod)
	{
		IFile *sf=Server->openFile("urbackup/data/settings.cfg", MODE_WRITE );
		if(sf==NULL)
		{
			Server->Log("Error opening settings file!", LL_ERROR);
			return;
		}

		sf->Write(Server->ConvertToUTF8(new_settings_str));
		Server->destroy(sf);
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
	
	Server->destroy(new_settings);
	IFile *sf=Server->openFile("urbackup/data/settings.cfg", MODE_WRITE);
	if(sf==NULL)
	{
		Server->Log("Error opening settings file!", LL_ERROR);
		return;
	}
	sf->Write(pData);
	if(pData.find("\r\nclient_set_settings=true")==std::string::npos)
	{
		sf->Write("\r\nclient_set_settings=true");
	}
	Server->destroy(sf);
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
				if(!isnumber(s_ltime[j]))
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
	thread_action=1;
	thread_ticket=Server->getThreadPool()->execute(this);
	state=4;
	IScopedLock lock(backup_mutex);
	backup_running=3;
	pcdone=0;
	backup_source_token=server_token;
	return true;
}

bool ClientConnector::sendIncrImage(void)
{
	thread_action=2;
	thread_ticket=Server->getThreadPool()->execute(this);
	state=5;
	IScopedLock lock(backup_mutex);
	backup_running=4;
	pcdone=0;
	pcdone2=0;
	backup_source_token=server_token;
	return true;
}

void ClientConnector::operator ()(void)
{
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_BEGIN
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
#endif
	if(thread_action==1)
	{
		sendFullImageThread();
	}
	else if(thread_action==2)
	{
		int timeouts=1800;
		while(hashdataok==false)
		{
			Server->wait(1000);
			--timeouts;
			if(timeouts<=0 || do_quit==true )
			{
				break;
			}
		}
		if(timeouts>0 || do_quit==true )
		{
			sendIncrImageThread();
		}
	}
	{
		IScopedLock lock(backup_mutex);
		if(backup_running==3 || backup_running==4)
		{
			backup_running=0;
		}
	}
#ifdef _WIN32
#ifdef THREAD_MODE_BACKGROUND_END
	SetThreadPriority( GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
#endif
#endif
}

bool ClientConnector::waitForThread(void)
{
	if(thread_action!=0 && Server->getThreadPool()->isRunning(thread_ticket ) )
		return true;
	else
		return false;
}

void ClientConnector::ImageErr(const std::string &msg)
{
	/*Server->Log(msg, LL_ERROR);
	unsigned int bs=0xFFFFFFFF;
	char *buffer=new char[sizeof(unsigned int)+msg.size()];
	memcpy(buffer, &bs, sizeof(unsigned int) );
	memcpy(&buffer[sizeof(unsigned int)], msg.c_str(), msg.size());
	pipe->Write(buffer, sizeof(unsigned int)+msg.size());
	delete [] buffer;*/
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

void ClientConnector::ImageErrRunning(const std::string &msg)
{
	Server->Log(msg, LL_ERROR);
	int64 bs=-124;
	char *buffer=new char[sizeof(int64)+msg.size()];
	memcpy(buffer, &bs, sizeof(int64) );
	memcpy(&buffer[sizeof(int64)], msg.c_str(), msg.size());
	pipe->Write(buffer, sizeof(int64)+msg.size());
	delete [] buffer;
}

void ClientConnector::sendFullImageThread(void)
{
	bool has_error=true;

	int save_id=-1;

	bool run=true;
	while(run)
	{
		if(shadowdrive.empty() && !no_shadowcopy)
		{
			std::string msg;
			mempipe->Read(&msg);
			if(msg.find("done")==0)
			{
				shadowdrive=getafter("-", msg);
				save_id=atoi(getuntil("-", shadowdrive).c_str());
				shadowdrive=getafter("-", shadowdrive);
				if(shadowdrive.size()>0 && shadowdrive[shadowdrive.size()-1]=='\\')
					shadowdrive.erase(shadowdrive.begin()+shadowdrive.size()-1);
				if(shadowdrive.empty())
				{
					ImageErr("Creating Shadow drive failed. Stopping. -2");
					run=false;
				}
			}
			else
			{
				ImageErr("Creating Shadow drive failed. Stopping.");
				run=false;
			}
			mempipe->Write("exit");
			mempipe=Server->createMemoryPipe();
		}
		else
		{
			IFilesystem *fs=image_fak->createFilesystem(Server->ConvertToUnicode(shadowdrive));
			if(fs==NULL)
			{
				ImageErr("Opening Shadow drive filesystem failed. Stopping.");
				run=false;
				break;
			}

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			int64 ncurrblocks=0;

			if(startpos==0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+shadowdrive.size()+sizeof(int)];
				char *cptr=buffer;
				memcpy(cptr, &blocksize, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &drivesize, sizeof(int64) );
				cptr+=sizeof(int64);
				memcpy(cptr, &blockcnt, sizeof(int64) );
				cptr+=sizeof(int64);
	#ifndef VSS_XP //persistence
	#ifndef VSS_S03
				*cptr=1;
	#else
				*cptr=0;
	#endif
	#else
				*cptr=0;
	#endif
				if(no_shadowcopy)
				{
					*cptr=0;
				}
				++cptr;
				unsigned int shadowdrive_size=(unsigned int)shadowdrive.size();
				memcpy(cptr, &shadowdrive_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &shadowdrive[0], shadowdrive.size());
				cptr+=shadowdrive.size();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);

				bool b=pipe->Write(buffer, sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+shadowdrive.size()+sizeof(int) );
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}
			
			ClientSend *cs=new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(cs);

			unsigned int needed_bufs=64;
			std::vector<char*> bufs;
			for(int64 i=startpos,blocks=drivesize/blocksize;i<blocks;i+=64)
			{
				std::vector<char*> n_bufs=cs->getBuffers(needed_bufs);
				bufs.insert(bufs.end(), n_bufs.begin(), n_bufs.end() );
				needed_bufs=0;

				unsigned int n=(unsigned int)(std::min)(blocks-i, (int64)64);
				std::vector<int64> secs=fs->readBlocks(i, n, bufs, sizeof(int64));
				needed_bufs+=(_u32)secs.size();
				for(size_t j=0;j<secs.size();++j)
				{
					memcpy(bufs[j], &secs[j], sizeof(int64) );
					cs->sendBuffer(bufs[j], sizeof(int64)+blocksize);
				}
				if(!secs.empty())
					bufs.erase(bufs.begin(), bufs.begin()+secs.size() );
				if(cs->hasError() )
				{
					Server->Log("Pipe broken -2", LL_ERROR);
					run=false;
					has_error=true;
					break;
				}

				ncurrblocks+=secs.size();
				
				if(!secs.empty() && IdleCheckerThread::getPause())
				{
					Server->wait(30000);
				}
			}

			for(size_t i=0;i<bufs.size();++i)
			{
				cs->freeBuffer(bufs[i]);
			}
			cs->doExit();
			std::vector<THREADPOOL_TICKET> wf;
			wf.push_back(send_ticket);
			Server->getThreadPool()->waitFor(wf);
			delete cs;

			if(!run)break;

			char lastbuffer[sizeof(int64)];
			int64 lastn=-123;
			memcpy(lastbuffer,&lastn, sizeof(int64));
			bool b=pipe->Write(lastbuffer, sizeof(int64));
			if(!b)
			{
				Server->Log("Pipe broken -3", LL_ERROR);
				run=false;
				has_error=true;
				break;
			}

			has_error=false;

			run=false;
		}
	}

	Server->Log("Sending full image done", LL_INFO);

#ifdef VSS_XP //persistence
	has_error=false;
#endif
#ifdef VSS_S03
	has_error=false;
#endif

	if(!has_error && !no_shadowcopy)
	{
		removeShadowCopyThread(save_id);
		std::string msg;
		mempipe->Read(&msg);
		if(msg.find("done")!=0)
		{
			Server->Log("Removing shadow copy failed in image streaming: "+msg, LL_ERROR);
		}
		mempipe->Write("exit");
		mempipe=Server->createMemoryPipe();
	}
	do_quit=true;
}

const unsigned int c_vhdblocksize=(1024*1024/2);
const unsigned int c_hashsize=32;

void ClientConnector::sendIncrImageThread(void)
{
	char *blockbuf=NULL;
	char *blockdatabuf=NULL;
	bool *has_blocks=NULL;
	char *zeroblockbuf=NULL;

	bool has_error=true;

	int save_id=-1;
	int update_cnt=0;

	unsigned int lastsendtime=Server->getTimeMS();

	bool run=true;
	while(run)
	{
		if(shadowdrive.empty())
		{
			std::string msg;
			mempipe->Read(&msg);
			if(msg.find("done")==0)
			{
				shadowdrive=getafter("-", msg);
				save_id=atoi(getuntil("-", shadowdrive).c_str());
				shadowdrive=getafter("-", shadowdrive);
				if(shadowdrive.size()>0 && shadowdrive[shadowdrive.size()-1]=='\\')
					shadowdrive.erase(shadowdrive.begin()+shadowdrive.size()-1);
				if(shadowdrive.empty())
				{
					ImageErr("Creating Shadow drive failed. Stopping. -2");
					run=false;
				}
			}
			else
			{
				ImageErr("Creating Shadow drive failed. Stopping.");
				run=false;
			}
			mempipe->Write("exit");
			mempipe=Server->createMemoryPipe();
		}
		else
		{
			IFilesystem *fs=image_fak->createFilesystem(Server->ConvertToUnicode(shadowdrive));
			if(fs==NULL)
			{
				ImageErr("Opening Shadow drive filesystem failed. Stopping.");
				run=false;
				break;
			}

			sha256_ctx shactx;
			unsigned int vhdblocks;

			unsigned int blocksize=(unsigned int)fs->getBlocksize();
			int64 drivesize=fs->getSize();
			int64 blockcnt=fs->calculateUsedSpace()/blocksize;
			vhdblocks=c_vhdblocksize/blocksize;
			int64 currvhdblock=0;
			int64 numblocks=drivesize/blocksize;

			if(startpos==0)
			{
				char *buffer=new char[sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+shadowdrive.size()+sizeof(int)];
				char *cptr=buffer;
				memcpy(cptr, &blocksize, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &drivesize, sizeof(int64) );
				cptr+=sizeof(int64);
				memcpy(cptr, &blockcnt, sizeof(int64) );
				cptr+=sizeof(int64);
	#ifndef VSS_XP //persistence
	#ifndef VSS_S03
				*cptr=1;
	#else
				*cptr=0;
	#endif
	#else
				*cptr=0;
	#endif
				++cptr;
				unsigned int shadowdrive_size=(unsigned int)shadowdrive.size();
				memcpy(cptr, &shadowdrive_size, sizeof(unsigned int));
				cptr+=sizeof(unsigned int);
				memcpy(cptr, &shadowdrive[0], shadowdrive.size());
				cptr+=shadowdrive.size();
				memcpy(cptr, &save_id, sizeof(int));
				cptr+=sizeof(int);
				bool b=pipe->Write(buffer, sizeof(unsigned int)+sizeof(int64)*2+1+sizeof(unsigned int)+shadowdrive.size()+sizeof(int) );
				if(!b)
				{
					Server->Log("Pipe broken -1", LL_ERROR);
					run=false;
					break;
				}
			}

			delete []blockbuf;
			blockbuf=new char[vhdblocks*blocksize];
			delete []has_blocks;
			has_blocks=new bool[vhdblocks];
			delete []zeroblockbuf;
			zeroblockbuf=new char[blocksize];
			memset(zeroblockbuf, 0, blocksize);

			ClientSend *cs=new ClientSend(pipe, blocksize+sizeof(int64), 2000);
			THREADPOOL_TICKET send_ticket=Server->getThreadPool()->execute(cs);


			for(int64 i=startpos,blocks=drivesize/blocksize;i<blocks;i+=vhdblocks)
			{
				++update_cnt;
				if(update_cnt>10)
				{
					IScopedLock lock(backup_mutex);
					pcdone2=(int)(((float)i/(float)blocks)*100.f+0.5f);
					update_cnt=0;
				}
				currvhdblock=i/vhdblocks;
				bool has_data=false;
				for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
				{
					if( fs->readBlock(j, NULL) )
					{
						has_data=true;
						break;
					}
				}

				if(has_data)
				{
					sha256_init(&shactx);
					bool mixed=false;
					for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
					{
						char *dpos=blockbuf+(j-i)*blocksize;
						if( fs->readBlock(j, dpos) )
						{
							sha256_update(&shactx, (unsigned char*)dpos, blocksize);
							has_blocks[j-i]=true;
						}
						else
						{
							sha256_update(&shactx, (unsigned char*)zeroblockbuf, blocksize);
							has_blocks[j-i]=false;
							mixed=true;
						}
					}
					unsigned char digest[c_hashsize];
					sha256_final(&shactx, digest);
					if(hashdatafile->Size()>=(currvhdblock+1)*c_hashsize)
					{
						hashdatafile->Seek(currvhdblock*c_hashsize);
						char hashdata_buf[c_hashsize];
						if( hashdatafile->Read(hashdata_buf, c_hashsize)!=c_hashsize )
						{
							Server->Log("Reading hashdata failed!", LL_ERROR);
						}
						if(memcmp(hashdata_buf, digest, c_hashsize)!=0)
						{
							//Server->Log("Block did change: "+nconvert(i)+" mixed="+nconvert(mixed), LL_DEBUG);
							for(int64 j=i;j<blocks && j<i+vhdblocks;++j)
							{
								if(has_blocks[j-i])
								{
									char* cb=cs->getBuffer();
									memcpy(&cb[sizeof(int64)], blockbuf+(j-i)*blocksize, blocksize);
									memcpy(cb, &j, sizeof(int64) );
									cs->sendBuffer(cb, sizeof(int64)+blocksize);

									lastsendtime=Server->getTimeMS();

									if(cs->hasError())
									{
										Server->Log("Pipe broken -2", LL_ERROR);
										run=false;
										break;
									}
								}
							}							
						}
						else
						{
							//Server->Log("Block didn't change: "+nconvert(i), LL_DEBUG);
							unsigned int tt=Server->getTimeMS();
							if(tt-lastsendtime>10000)
							{
								int64 bs=-125;
								char* buffer=cs->getBuffer();
								memcpy(buffer, &bs, sizeof(int64) );
								cs->sendBuffer(buffer, sizeof(int64));

								lastsendtime=tt;
							}
						}
					}
					else
					{
						ImageErrRunning("Hashdata from server too small");
						run=false;
						break;
					}
					if(!run)break;
				}
				else
				{
					unsigned int tt=Server->getTimeMS();
					if(tt-lastsendtime>10000)
					{
						int64 bs=-125;
						char* buffer=cs->getBuffer();
						memcpy(buffer, &bs, sizeof(int64) );
						cs->sendBuffer(buffer, sizeof(int64));

						lastsendtime=tt;
					}
				}

				if(IdleCheckerThread::getPause())
				{
					Server->wait(30000);
				}
			}

			cs->doExit();
			std::vector<THREADPOOL_TICKET> wf;
			wf.push_back(send_ticket);
			Server->getThreadPool()->waitFor(wf);
			delete cs;

			if(!run)break;

			char lastbuffer[sizeof(int64)];
			int64 lastn=-123;
			memcpy(lastbuffer,&lastn, sizeof(int64));
			bool b=pipe->Write(lastbuffer, sizeof(int64));
			if(!b)
			{
				Server->Log("Pipe broken -3", LL_ERROR);
				run=false;
				break;
			}

			has_error=false;

			run=false;
		}
	}

	delete [] blockbuf;
	delete [] has_blocks;
	delete [] zeroblockbuf;

	Server->destroy(hashdatafile);

	Server->Log("Sending image done", LL_INFO);

#ifdef VSS_XP //persistence
	has_error=false;
#endif
#ifdef VSS_S03
	has_error=false;
#endif

	if(!has_error)
	{
		removeShadowCopyThread(save_id);
		std::string msg;
		mempipe->Read(&msg);
		if(msg.find("done")!=0)
		{
			Server->Log("Removing shadow copy failed in image streaming: "+msg, LL_ERROR);
		}
		mempipe->Write("exit");
		mempipe=Server->createMemoryPipe();
	}
	do_quit=true;
}

void ClientConnector::removeShadowCopyThread(int save_id)
{
	if(!no_shadowcopy)
	{
		CWData data;
		data.addChar(3);
		data.addVoidPtr(mempipe);
		data.addString(image_letter);
		data.addString(server_token);
		data.addUChar(1);
		data.addInt(save_id);

		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	}
}

bool ClientConnector::sendMBR(std::wstring dl)
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
		Server->Log(L"CreateFile of volume '"+dl+L"' failed. - sendMBR", LL_ERROR);
		return false;
	}

	STORAGE_DEVICE_NUMBER dev_num;
	DWORD ret_bytes;
	BOOL b=DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &dev_num, sizeof(STORAGE_DEVICE_NUMBER), &ret_bytes, NULL);
	
	if(b==0)
	{
		Server->Log(L"DeviceIoControl IOCTL_STORAGE_GET_DEVICE_NUMBER failed. Volume: '"+dl+L"'", LL_WARNING);

		VOLUME_DISK_EXTENTS *vde=(VOLUME_DISK_EXTENTS*)new char[sizeof(VOLUME_DISK_EXTENTS)];
		b=DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, sizeof(VOLUME_DISK_EXTENTS), &ret_bytes, NULL);
		if(b==0 && GetLastError()==ERROR_MORE_DATA)
		{
			DWORD ext_num=vde->NumberOfDiskExtents;
			Server->Log(L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed. Extends: "+convert((int)ext_num), LL_WARNING);
			delete []vde;
			DWORD vde_size=sizeof(VOLUME_DISK_EXTENTS)+sizeof(DISK_EXTENT)*(ext_num-1);
			vde=(VOLUME_DISK_EXTENTS*)new char[vde_size];
			b=DeviceIoControl(hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, vde, vde_size, &ret_bytes, NULL);
			if(b==0)
			{
				Server->Log(L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed twice. Volume: '"+dl+L"'", LL_ERROR);
				delete []vde;
				CloseHandle(hVolume);
				return false;
			}
		}
		else if(b==0)
		{
			Server->Log(L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed. Volume: '"+dl+L"' Error: "+convert((int)GetLastError()), LL_ERROR);
			delete []vde;
			CloseHandle(hVolume);
			return false;
		}

		if(vde->NumberOfDiskExtents>0)
		{
			HANDLE hDevice=CreateFileW((L"\\\\.\\PhysicalDrive"+convert((int)vde->Extents[0].DiskNumber)).c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(hDevice==INVALID_HANDLE_VALUE)
			{
				Server->Log(L"CreateFile of device '"+dl+L"' failed. - sendMBR", LL_ERROR);
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
				Server->Log(L"DeviceIoControl IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed. Volume: '"+dl+L"' Error: "+convert((int)GetLastError()), LL_ERROR);
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
				Server->Log(L"Dynamic volumes are not supported. It may work with mirrored whole disk volumes though. Volume: '"+dl+L"'", LL_WARNING);
			}
			else
			{
				Server->Log(L"Did not find PartitionNumber of dynamic volume. Volume: '"+dl+L"'", LL_ERROR);
				CloseHandle(hVolume);
				return false;
			}
		}
		else
		{
			Server->Log(L"DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS returned no extends. Volume: '"+dl+L"'", LL_ERROR);
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
		Server->Log(L"GetVolumeInformationW failed. Volume: '"+dl+L"'", LL_ERROR);
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
		Server->Log(L"Error opening Device "+convert((int)dev_num.DeviceNumber), LL_ERROR);
		return false;
	}

	std::string mbr_bytes=dev->Read(512);

	mbr.addString(mbr_bytes);

	tcpstack.Send(pipe, mbr);

	return true;
#endif //WIN_32
}

const unsigned int receive_timeouttime=60000;

std::string ClientConnector::receivePacket(IPipe *p)
{
	unsigned int starttime=Server->getTimeMS();
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
		IPipe *c=channel_pipes[i];
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
			continue;
		}
		if(!pipe->Write((char*)&imgsize, sizeof(_i64), (int)receive_timeouttime))
		{
			Server->Log("Could not write to pipe! downloadImage-1", LL_ERROR);
			return;
		}

		char buf[4096];
		_i64 read=0;

		if(params[L"mbr"]==L"true")
		{
			Server->Log("Downloading MBR...", LL_DEBUG);
			while(read<imgsize)
			{
				size_t c_read=c->Read(buf, 4096, 60000);
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
			size_t r=c->Read(&buf[off], 4096-off, 180000);
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
						blockleft=4096;
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
						char buf2[4096];
						memcpy(buf2, &buf[off], r-off);
						memcpy(buf, buf2, r-off);
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
	imgsize=-1;
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

unsigned int ClientConnector::getLastTokenTime(const std::string & tok)
{
	IScopedLock lock(backup_mutex);

	std::map<std::string, unsigned int>::iterator it=last_token_times.find(tok);
	if(it!=last_token_times.end())
	{
		return it->second;
	}
	else
	{
		return 0;
	}
}