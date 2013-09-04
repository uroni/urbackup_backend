#include "../Interface/Server.h"
#include "ClientService.h"
#include "ServerIdentityMgr.h"
#include "../common/data.h"
#include "../urbackupcommon/capa_bits.h"
#include "../urbackupcommon/escape.h"
#include "client.h"
#include "database.h"
#include "../stringtools.h"
#ifdef _WIN32
#include "win_sysvol.h"
#include "win_ver.h"
#else
std::wstring getSysVolume(std::wstring &mpath){ return L""; }
#endif
#include "../client_version.h"

#include <stdlib.h>
#include <limits.h>

#ifndef _WIN32
#define _atoi64 atoll
#endif

extern std::string time_format_str;

void ClientConnector::CMD_ADD_IDENTITY(const std::string &identity, const std::string &cmd, bool ident_ok)
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
			ServerIdentityMgr::loadServerIdentities();
			if( ServerIdentityMgr::checkServerIdentity(identity) )
			{
				tcpstack.Send(pipe, "OK");
				return;
			}

			if( ServerIdentityMgr::numServerIdentities()==0 )
			{
				ServerIdentityMgr::addServerIdentity(identity);
				tcpstack.Send(pipe, "OK");
			}
			else
			{
				if( !ServerIdentityMgr::hasOnlineServer() && ServerIdentityMgr::isNewIdentity(identity) )
				{
					IScopedLock lock(ident_mutex);
					new_server_idents.push_back(identity);
				}
				tcpstack.Send(pipe, "failed");
			}
		}
	}
}

void ClientConnector::CMD_START_INCR_FILEBACKUP(const std::string &cmd)
{
	if(cmd=="2START BACKUP") file_version=2;

	state=CCSTATE_START_FILEBACKUP;

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(0);
	data.addVoidPtr(mempipe);
	data.addString(server_token);
	data.addInt(end_to_end_file_backup_verification_enabled?1:0);
	data.addInt(calculateFilehashesOnClient()?1:0);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	lasttime=Server->getTimeMS();

	backup_running=RUNNING_INCR_FILE;
	last_pingtime=Server->getTimeMS();
	pcdone=0;
	backup_source_token=server_token;
}

void ClientConnector::CMD_START_FULL_FILEBACKUP(const std::string &cmd)
{
	if(cmd=="2START FULL BACKUP") file_version=2;

	state=CCSTATE_START_FILEBACKUP;

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(1);
	data.addVoidPtr(mempipe);
	data.addString(server_token);
	data.addInt(end_to_end_file_backup_verification_enabled?1:0);
	data.addInt(calculateFilehashesOnClient()?1:0);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	lasttime=Server->getTimeMS();

	backup_running=RUNNING_FULL_FILE;
	last_pingtime=Server->getTimeMS();
	pcdone=0;
	backup_source_token=server_token;
}

void ClientConnector::CMD_START_SHADOWCOPY(const std::string &cmd)
{
#ifdef _WIN32
	if(cmd[cmd.size()-1]=='"')
	{
		state=CCSTATE_SHADOWCOPY;
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

void ClientConnector::CMD_STOP_SHADOWCOPY(const std::string &cmd)
{
#ifdef _WIN32
	if(cmd[cmd.size()-1]=='"')
	{
		state=CCSTATE_SHADOWCOPY;
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

void ClientConnector::CMD_SET_INCRINTERVAL(const std::string &cmd)
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

void ClientConnector::CMD_GET_BACKUPDIRS(const std::string &cmd)
{
	int version=0;
	if(!cmd.empty() && cmd[0]=='1')
		version=1;

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

	lasttime=Server->getTimeMS();
}

void ClientConnector::CMD_SAVE_BACKUPDIRS(const std::string &cmd, str_map &params)
{
	if(saveBackupDirs(params))
	{
		tcpstack.Send(pipe, "OK");
	}
	lasttime=Server->getTimeMS();
}

void ClientConnector::CMD_GET_INCRINTERVAL(const std::string &cmd)
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

void ClientConnector::CMD_DID_BACKUP(const std::string &cmd)
{
	updateLastBackup();
	tcpstack.Send(pipe, "OK");

	{
		IScopedLock lock(backup_mutex);
		if(backup_running==RUNNING_INCR_FILE || backup_running==RUNNING_FULL_FILE)
		{
			backup_running=RUNNING_NONE;
			backup_done=true;
		}
		lasttime=Server->getTimeMS();
	}
			
	IndexThread::execute_postbackup_hook();
}

void ClientConnector::CMD_STATUS(const std::string &cmd)
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

	if(backup_running==RUNNING_NONE)
	{
		if(backup_done)
			ret+="#DONE";
		else
			ret+="#NOA";

		backup_done=false;
	}
	else if(backup_running==RUNNING_INCR_FILE)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#INCR";
	}
	else if(backup_running==RUNNING_FULL_FILE)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#FULL";
	}
	else if(backup_running==RUNNING_FULL_IMAGE)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#FULLI";
	}
	else if(backup_running==RUNNING_INCR_IMAGE)
	{
		if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout )
			ret+="#NOA";
		else
			ret+="#INCRI";
	}

	if(backup_running!=RUNNING_INCR_IMAGE)
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

	{
		IScopedLock lock(ident_mutex);
		if(!new_server_idents.empty())
		{
			ret+="&new_ident="+new_server_idents[new_server_idents.size()-1];
			new_server_idents.erase(new_server_idents.begin()+new_server_idents.size()-1);
		}
	}

	tcpstack.Send(pipe, ret);

	db->destroyAllQueries();

	lasttime=Server->getTimeMS();
}

void ClientConnector::CMD_UPDATE_SETTINGS(const std::string &cmd)
{
	std::string s_settings=cmd.substr(9);
	unescapeMessage(s_settings);
	updateSettings( s_settings );
	tcpstack.Send(pipe, "OK");
	lasttime=Server->getTimeMS();
}

void ClientConnector::CMD_PING_RUNNING(const std::string &cmd)
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

void ClientConnector::CMD_CHANNEL(const std::string &cmd, IScopedLock *g_lock)
{
	if(!img_download_running)
	{
		g_lock->relock(backup_mutex);
		channel_pipe=SChannel(pipe, internet_conn);
		channel_pipes.push_back(SChannel(pipe, internet_conn));
		is_channel=true;
		state=CCSTATE_CHANNEL;
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

void ClientConnector::CMD_CHANNEL_PONG(const std::string &cmd)
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

void ClientConnector::CMD_CHANNEL_PING(const std::string &cmd)
{
	lasttime=Server->getTimeMS();
	if(tcpstack.Send(pipe, "PONG")==0)
	{
		do_quit=true;
	}
}

void ClientConnector::CMD_TOCHANNEL_START_INCR_FILEBACKUP(const std::string &cmd)
{
	tochannelSendStartbackup(RUNNING_INCR_FILE);
}

void ClientConnector::CMD_TOCHANNEL_START_FULL_FILEBACKUP(const std::string &cmd)
{
	tochannelSendStartbackup(RUNNING_FULL_FILE);
}

void ClientConnector::CMD_TOCHANNEL_START_FULL_IMAGEBACKUP(const std::string &cmd)
{
	tochannelSendStartbackup(RUNNING_FULL_IMAGE);
}

void ClientConnector::CMD_TOCHANNEL_START_INCR_IMAGEBACKUP(const std::string &cmd)
{
	tochannelSendStartbackup(RUNNING_INCR_IMAGE);
}

void ClientConnector::CMD_TOCHANNEL_UPDATE_SETTINGS(const std::string &cmd)
{
	std::string s_settings=cmd.substr(16);
	lasttime=Server->getTimeMS();
	unescapeMessage(s_settings);
	replaceSettings( s_settings );

	IScopedLock lock(backup_mutex);
	bool ok=false;
	for(size_t o=0;o<channel_pipes.size();++o)
	{
		CTCPStack tmpstack(channel_pipes[o].internet_connection);
		_u32 rc=(_u32)tmpstack.Send(channel_pipes[o].pipe, "UPDATE SETTINGS");
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

void ClientConnector::CMD_LOGDATA(const std::string &cmd)
{
	std::string ldata=cmd.substr(9);
	size_t cpos=ldata.find(" ");
	std::string created=getuntil(" ", ldata);
	lasttime=Server->getTimeMS();
	saveLogdata(created, getafter(" ",ldata));
	tcpstack.Send(pipe, "OK");
}

void ClientConnector::CMD_PAUSE(const std::string &cmd)
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

void ClientConnector::CMD_GET_LOGPOINTS(const std::string &cmd)
{
	lasttime=Server->getTimeMS();
	tcpstack.Send(pipe, getLogpoints() );
}

void ClientConnector::CMD_GET_LOGDATA(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	int logid=watoi(params[L"logid"]);
	int loglevel=watoi(params[L"loglevel"]);
	std::string ret;
	getLogLevel(logid, loglevel, ret);
	tcpstack.Send(pipe, ret);
}

void ClientConnector::CMD_FULL_IMAGE(const std::string &cmd, bool ident_ok)
{
	if(ident_ok==true)
	{
		lasttime=Server->getTimeMS();
		std::string s_params=cmd.substr(11);
		str_map params;
		ParseParamStr(s_params, &params);

		server_token=Server->ConvertToUTF8(params[L"token"]);
		image_inf.image_letter=Server->ConvertToUTF8(params[L"letter"]);
		image_inf.shadowdrive=Server->ConvertToUTF8(params[L"shadowdrive"]);
		if(params.find(L"start")!=params.end())
		{
			image_inf.startpos=(uint64)_atoi64(Server->ConvertToUTF8(params[L"start"]).c_str());
		}
		else
		{
			image_inf.startpos=0;
		}
		if(params.find(L"shadowid")!=params.end())
		{
			image_inf.shadow_id=watoi(params[L"shadowid"]);
		}
		else
		{
			image_inf.shadow_id=-1;
		}
		image_inf.with_checksum=false;
		if(params.find(L"checksum")!=params.end())
		{
			if(params[L"checksum"]==L"1")
				image_inf.with_checksum=true;
		}

		image_inf.no_shadowcopy=false;

		if(image_inf.image_letter=="SYSVOL")
		{
			std::wstring mpath;
			std::wstring sysvol=getSysVolume(mpath);
			if(!mpath.empty())
			{
				image_inf.image_letter=Server->ConvertToUTF8(mpath);
			}
			else
			{
				image_inf.image_letter=Server->ConvertToUTF8(sysvol);
				image_inf.no_shadowcopy=true;
			}
		}

		if(image_inf.startpos==0 && !image_inf.no_shadowcopy)
		{
			CWData data;
			data.addChar(2);
			data.addVoidPtr(mempipe);
			data.addString(image_inf.image_letter);
			data.addString(server_token);
			data.addUChar(1); //image backup
			data.addUChar(0); //filesrv
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
		}
		else if(image_inf.shadow_id!=-1)
		{
			image_inf.shadowdrive.clear();
			CWData data;
			data.addChar(4);
			data.addVoidPtr(mempipe);
			data.addInt(image_inf.shadow_id);
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
		}

		if(image_inf.no_shadowcopy)
		{
			image_inf.shadowdrive=image_inf.image_letter;
			if(!image_inf.shadowdrive.empty() && image_inf.shadowdrive[0]!='\\')
			{
				image_inf.shadowdrive="\\\\.\\"+image_inf.image_letter;
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

void ClientConnector::CMD_INCR_IMAGE(const std::string &cmd, bool ident_ok)
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
			image_inf.image_letter=Server->ConvertToUTF8(params[L"letter"]);
			image_inf.shadowdrive=Server->ConvertToUTF8(params[L"shadowdrive"]);
			if(params.find(L"start")!=params.end())
			{
				image_inf.startpos=(uint64)_atoi64(Server->ConvertToUTF8(params[L"start"]).c_str());
			}
			else
			{
				image_inf.startpos=0;
			}
			if(params.find(L"shadowid")!=params.end())
			{
				image_inf.shadow_id=watoi(params[L"shadowid"]);
			}
			else
			{
				image_inf.shadow_id=-1;
			}
			image_inf.with_checksum=false;
			if(params.find(L"checksum")!=params.end())
			{
				if(params[L"checksum"]==L"1")
					image_inf.with_checksum=true;
			}

			image_inf.no_shadowcopy=false;

			if(image_inf.startpos==0)
			{
				CWData data;
				data.addChar(2);
				data.addVoidPtr(mempipe);
				data.addString(image_inf.image_letter);
				data.addString(server_token);
				data.addUChar(1); //image backup
				data.addUChar(0); //file serv?
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			}
			else if(image_inf.shadow_id!=-1)
			{
				image_inf.shadowdrive.clear();
				CWData data;
				data.addChar(4);
				data.addVoidPtr(mempipe);
				data.addInt(image_inf.shadow_id);
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			}
			hashdatafile=Server->openTemporaryFile();
			if(hashdatafile==NULL)
			{
				Server->Log("Error creating temporary file in CMD_INCR_IMAGE", LL_ERROR);
				do_quit=true;
				return;
			}

			if(tcpstack.getBuffersize()>0)
			{
				if(hashdatafile->Write(tcpstack.getBuffer(), (_u32)tcpstack.getBuffersize())!=tcpstack.getBuffersize())
				{
					Server->Log("Error writing to hashdata temporary file in CMD_INCR_IMAGE", LL_ERROR);
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
					Server->Log("Too much hashdata - error in CMD_INCR_IMAGE", LL_ERROR);
				}

				if(hashdataleft==0)
				{
					hashdataok=true;
					state=CCSTATE_IMAGE;
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

void ClientConnector::CMD_MBR(const std::string &cmd)
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
	std::wstring errmsg;
	if(!dl.empty())
	{
		b=sendMBR(dl, errmsg);
	}
	if(!b)
	{
		CWData r;
		r.addChar(0);
		r.addString(Server->ConvertToUTF8(errmsg));
		tcpstack.Send(pipe, r);
	}
}

void ClientConnector::CMD_RESTORE_GET_BACKUPCLIENTS(const std::string &cmd)
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
			tcpstack.Send(channel_pipes[i].pipe, "GET BACKUPCLIENTS");
			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);
			std::string nc=receivePacket(channel_pipes[i].pipe);
			if(channel_pipes[i].pipe->hasError())
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

void ClientConnector::CMD_RESTORE_GET_BACKUPIMAGES(const std::string &cmd)
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
			tcpstack.Send(channel_pipes[i].pipe, cmd);
			std::string nc=receivePacket(channel_pipes[i].pipe);
			if(!nc.empty())
			{
				imgs+=nc+"\n";
			}
		}
		tcpstack.Send(pipe, "1"+imgs);
	}
}

void ClientConnector::CMD_RESTORE_DOWNLOAD_IMAGE(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	Server->Log("Downloading image...", LL_DEBUG);
	IScopedLock lock(backup_mutex);
	waitForPings(&lock);
	Server->Log("In mutex...",LL_DEBUG);
	img_download_running=true;
	downloadImage(params);
	img_download_running=false;
	Server->Log("Download done -2", LL_DEBUG);
	do_quit=true;
}

void ClientConnector::CMD_RESTORE_DOWNLOADPROGRESS(const std::string &cmd)
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

void ClientConnector::CMD_RESTORE_LOGIN_FOR_DOWNLOAD(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	IScopedLock lock(backup_mutex);
	waitForPings(&lock);
	if(channel_pipes.size()==0)
	{
		tcpstack.Send(pipe, "no channels available");
	}
	else
	{
		bool one_ok=false;
		std::string errors;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
			tcpstack.Send(channel_pipes[i].pipe, "LOGIN username="+Server->ConvertToUTF8(params[L"username"])
													+"&password="+Server->ConvertToUTF8(params[L"password"]));

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);

			std::string nc=receivePacket(channel_pipes[i].pipe);

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after read -1", LL_DEBUG);

			Server->Log("Client "+nconvert(i)+"/"+nconvert(channel_pipes.size())+": --"+nc+"--", LL_DEBUG);

			if(nc=="ok")
			{
				one_ok=true;
			}
			else
			{
				Server->Log("Client "+nconvert(i)+"/"+nconvert(channel_pipes.size())+" ERROR: --"+nc+"--", LL_ERROR);
				if(!errors.empty())
				{
					errors+=" -- ";
				}
				errors+=nc;
			}
		}
		if(one_ok)
		{
			tcpstack.Send(pipe, "ok");
		}
		else
		{
			tcpstack.Send(pipe, errors);
		}
	}
}

void ClientConnector::CMD_VERSION_UPDATE(const std::string &cmd)
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

void ClientConnector::CMD_CLIENT_UPDATE(const std::string &cmd)
{
	hashdatafile=Server->openTemporaryFile();
	if(hashdatafile==NULL)
	{
		Server->Log("Error creating temporary file in CMD_CLIENT_UPDATE", LL_ERROR);
		do_quit=true;
		return;
	}
	
	if(cmd.find("CLIENTUPDATE")==0)
	{
		hashdataleft=atoi(cmd.substr(13).c_str());
		silent_update=false;
	}
	else
	{
		str_map params;
		ParseParamStr(cmd.substr(14), &params);

		hashdataleft=watoi(params[L"size"]);
		silent_update=(params[L"silent_update"]==L"true");
	}
	hashdataok=false;
	state=CCSTATE_UPDATE_DATA;

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
		}
		else
		{
			Server->Log("Too much hashdata - error -1update", LL_ERROR);
		}

		if(hashdataleft==0)
		{
			hashdataok=true;
			state=CCSTATE_UPDATE_FINISH;
			return;
		}
	}
	return;
}

void ClientConnector::CMD_CAPA(const std::string &cmd)
{
	std::string client_version_str=std::string(c_client_version);
#ifdef _WIN32
	std::wstring buf;
	buf.resize(1024);
	std::string os_version_str;
	if( GetOSDisplayString(const_cast<wchar_t*>(buf.c_str())) )
	{
		os_version_str=Server->ConvertToUTF8(std::wstring(buf.c_str()));
	}

	tcpstack.Send(pipe, "FILE=2&IMAGE=1&UPDATE=1&MBR=1&FILESRV=2&SET_SETTINGS=1&IMAGE_VER=1&CLIENTUPDATE=1"
		"&CLIENT_VERSION_STR="+EscapeParamString(client_version_str)+"&OS_VERSION_STR="+EscapeParamString(os_version_str));
#else
	std::string os_version_str="not Windows";
	tcpstack.Send(pipe, "FILE=2&FILESRV=2&SET_SETTINGS=1&CLIENTUPDATE=1"
		"&CLIENT_VERSION_STR="+EscapeParamString(client_version_str)+"&OS_VERSION_STR="+EscapeParamString(client_version_str));
#endif
}

void ClientConnector::CMD_NEW_SERVER(str_map &params)
{
	std::string ident=Server->ConvertToUTF8(params[L"ident"]);
	if(!ident.empty())
	{
		ServerIdentityMgr::addServerIdentity(ident);
		tcpstack.Send(pipe, "OK");
	}
	else
	{
		tcpstack.Send(pipe, "FAILED");
	}
}

void ClientConnector::CMD_ENABLE_END_TO_END_FILE_BACKUP_VERIFICATION(const std::string &cmd)
{
	IScopedLock lock(backup_mutex);
	end_to_end_file_backup_verification_enabled=true;
	tcpstack.Send(pipe, "OK");
}

void ClientConnector::CMD_GET_VSSLOG(const std::string &cmd)
{
	CWData data;
	data.addChar(IndexThread::IndexThreadAction_GetLog);
	data.addVoidPtr(mempipe);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	std::string ret;
	mempipe->Read(&ret, 8000);
	tcpstack.Send(pipe, ret);
	mempipe->Write("exit");
	mempipe=Server->createMemoryPipe();
}