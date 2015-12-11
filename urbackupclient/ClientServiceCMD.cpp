/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#include "RestoreFiles.h"
#include "../Interface/Server.h"
#include "../Interface/SettingsReader.h"
#include "ClientService.h"
#include "InternetClient.h"
#include "ServerIdentityMgr.h"
#include "../common/data.h"
#include "../urbackupcommon/capa_bits.h"
#include "../urbackupcommon/escape.h"
#include "client.h"
#include "database.h"
#include "../stringtools.h"
#include "../urbackupcommon/json.h"
#include "../cryptoplugin/ICryptoFactory.h"
#ifdef _WIN32
#include "win_sysvol.h"
#include "win_ver.h"
#include "win_all_volumes.h"
#include "DirectoryWatcherThread.h"
#else
#include "lin_ver.h"
std::wstring getSysVolume(std::wstring &mpath){ return L""; }
std::wstring getEspVolume(std::wstring &mpath){ return L""; }
#endif
#include "../client_version.h"

#include <stdlib.h>
#include <limits.h>


#ifndef _WIN32
#define _atoi64 atoll
#endif

extern std::string time_format_str;

extern ICryptoFactory *crypto_fak;

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
			ServerIdentityMgr::addServerIdentity(identity, "");
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
				if(ServerIdentityMgr::hasPublicKey(identity))
				{
					tcpstack.Send(pipe, "needs certificate");
				}
				else
				{
					tcpstack.Send(pipe, "OK");
				}				
				return;
			}

			if( ServerIdentityMgr::numServerIdentities()==0 )
			{
				ServerIdentityMgr::addServerIdentity(identity, "");
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

void ClientConnector::CMD_GET_CHALLENGE(const std::string &identity)
{
	if(identity.empty())
	{
		tcpstack.Send(pipe, "");
		return;
	}

	IScopedLock lock(ident_mutex);
	std::string challenge = Server->secureRandomString(30)+"-"+nconvert(Server->getTimeSeconds())+"-"+nconvert(Server->getTimeMS());
	challenges[identity]=challenge;

	tcpstack.Send(pipe, challenge);
}

void ClientConnector::CMD_SIGNATURE(const std::string &identity, const std::string &cmd)
{
	if(identity.empty())
	{
		Server->Log("Signature error: Empty identity", LL_ERROR);
		tcpstack.Send(pipe, "empty identity");
		return;
	}
	if(crypto_fak==NULL)
	{
		Server->Log("Signature error: No crypto module", LL_ERROR);
		tcpstack.Send(pipe, "no crypto");
		return;
	}

	str_nmap::iterator challenge_it = challenges.find(identity);

	if(challenge_it==challenges.end() || challenge_it->second.empty())
	{
		Server->Log("Signature error: No challenge", LL_ERROR);
		tcpstack.Send(pipe, "no challenge");
		return;
	}

	const std::string& challenge = challenge_it->second;

	size_t hashpos = cmd.find("#");
	if(hashpos==std::string::npos)
	{
		Server->Log("Signature error: No parameters", LL_ERROR);
		tcpstack.Send(pipe, "no parameters");
		return;
	}

	str_map params;
	ParseParamStrHttp(cmd.substr(hashpos+1), &params);

	std::string pubkey = base64_decode_dash(wnarrow(params[L"pubkey"]));
	std::string signature = base64_decode_dash(wnarrow(params[L"signature"]));
	std::string session_identity = Server->ConvertToUTF8(params[L"session_identity"]);

	if(!ServerIdentityMgr::hasPublicKey(identity))
	{
		ServerIdentityMgr::setPublicKey(identity, pubkey);
	}

	pubkey = ServerIdentityMgr::getPublicKey(identity);
	
	if(crypto_fak->verifyData(pubkey, challenge, signature))
	{
		ServerIdentityMgr::addSessionIdentity(session_identity, endpoint_name);
		tcpstack.Send(pipe, "ok");
		challenges.erase(challenge_it);
	}
	else
	{
		Server->Log("Signature error: Verification failed", LL_ERROR);
		tcpstack.Send(pipe, "signature verification failed");
	}
}

void ClientConnector::CMD_START_INCR_FILEBACKUP(const std::string &cmd)
{
	std::string s_params;
	if(next(cmd, 0, "3START BACKUP"))
	{
		file_version=2;
		if(cmd.size()>14)
			s_params=cmd.substr(14);
	}
	else if(cmd=="2START BACKUP")
	{
		file_version=2;
	}

	str_map params;
	if(!s_params.empty())
	{
		ParseParamStrHttp(s_params, &params);
	}

	std::wstring resume = params[L"resume"];

	int group=c_group_default;

	str_map::iterator it_group = params.find(L"group");
	if(it_group!=params.end())
	{
		group = watoi(it_group->second);
	}

	unsigned int flags = 0;

	if(params.find(L"with_scripts")!=params.end())
	{
		flags |= flag_with_scripts;
	}

	if(params.find(L"with_orig_path")!=params.end())
	{
		flags |= flag_with_orig_path;
	}

	if(params.find(L"with_sequence")!=params.end())
	{
		flags |= flag_with_sequence;
	}

	if(params.find(L"with_proper_symlinks")!=params.end())
	{
		flags |= flag_with_proper_symlinks;
	}

	if(end_to_end_file_backup_verification_enabled)
	{
		flags |= flag_end_to_end_verification;
	}

	if(calculateFilehashesOnClient())
	{
		flags |= flag_calc_checksums;
	}

	state=CCSTATE_START_FILEBACKUP;

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(0);
	data.addVoidPtr(mempipe);
	data.addString(server_token);
	data.addInt(group);
	data.addInt(flags);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	mempipe_owner=false;

	lasttime=Server->getTimeMS();

	if(resume.empty())
	{
		backup_running=RUNNING_INCR_FILE;
	}
	else if(resume==L"full")
	{
		backup_running=RUNNING_RESUME_FULL_FILE;
	}
	else if(resume==L"incr")
	{
		backup_running=RUNNING_RESUME_INCR_FILE;
	}

	status_updated = true;

	end_to_end_file_backup_verification_enabled=false;
	
	last_pingtime=Server->getTimeMS();
	pcdone=-1;
	backup_source_token=server_token;
}

void ClientConnector::CMD_START_FULL_FILEBACKUP(const std::string &cmd)
{
	std::string s_params;
	if(cmd=="2START FULL BACKUP")
	{
		file_version=2;
	}
	else if(next(cmd,0,"3START FULL BACKUP"))
	{
		file_version=2;
		if(cmd.size()>19)
			s_params=cmd.substr(19);
	}

	str_map params;
	if(!s_params.empty())
	{
		ParseParamStrHttp(s_params, &params);
	}

	int group=c_group_default;

	str_map::iterator it_group = params.find(L"group");
	if(it_group!=params.end())
	{
		group = watoi(it_group->second);
	}

	int flags = 0;

	if(params.find(L"with_scripts")!=params.end())
	{
		flags |= flag_with_scripts;
	}

	if(params.find(L"with_orig_path")!=params.end())
	{
		flags |= flag_with_orig_path;
	}

	if(params.find(L"with_sequence")!=params.end())
	{
		flags |= flag_with_sequence;
	}

	if(params.find(L"with_proper_symlinks")!=params.end())
	{
		flags |= flag_with_proper_symlinks;
	}

	if(end_to_end_file_backup_verification_enabled)
	{
		flags |= flag_end_to_end_verification;
	}

	if(calculateFilehashesOnClient())
	{
		flags |= flag_calc_checksums;
	}

	state=CCSTATE_START_FILEBACKUP;

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(1);
	data.addVoidPtr(mempipe);
	data.addString(server_token);
	data.addInt(group);
	data.addInt(flags);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	mempipe_owner=false;

	lasttime=Server->getTimeMS();

	end_to_end_file_backup_verification_enabled=false;

	backup_running=RUNNING_FULL_FILE;
	status_updated = true;
	last_pingtime=Server->getTimeMS();
	pcdone=-1;
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
		mempipe_owner=false;
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
		mempipe_owner=false;
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
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT id,name,path,tgroup FROM backupdirs WHERE symlinked=0");
	int timeoutms=300;
	db_results res=q->Read(&timeoutms);

	if(timeoutms==0)
	{
		JSON::Object ret;

		JSON::Array dirs;
		for(size_t i=0;i<res.size();++i)
		{
			if(res[i][L"name"]==L"*") continue;

			JSON::Object cdir;

			cdir.set("id", watoi(res[i][L"id"]));
			cdir.set("name", res[i][L"name"]);
			cdir.set("path", res[i][L"path"]);
			cdir.set("group", watoi(res[i][L"tgroup"]));

			dirs.add(cdir);
		}

		ret.set("dirs", dirs);

		tcpstack.Send(pipe, ret.get(true));
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
	if(last_capa & DONT_ALLOW_CONFIG_PATHS)
	{
		tcpstack.Send(pipe, "FAILED");
		return;
	}

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
		if(backup_running==RUNNING_INCR_FILE || backup_running==RUNNING_FULL_FILE ||
			backup_running==RUNNING_RESUME_INCR_FILE || backup_running==RUNNING_RESUME_FULL_FILE )
		{
			backup_running=RUNNING_NONE;
			backup_done=true;
		}
		lasttime=Server->getTimeMS();
	}

	status_updated = true;
			
	IndexThread::execute_postbackup_hook();
}

std::string ClientConnector::getLastBackupTime()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT strftime('"+time_format_str+"',last_backup, 'localtime') AS last_backup FROM status");
	if(q==NULL)
		return std::string();

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

	if(res.size()>0)
	{
		return Server->ConvertToUTF8(res[0][L"last_backup"]);
	}
	else
	{
		return std::string();
	}
}

std::string ClientConnector::getCurrRunningJob()
{
	if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout)
	{
		return "NOA";
	}

	if(backup_running==RUNNING_NONE)
	{
		if(backup_done)
			return "DONE";
		else
			return "NOA";

		backup_done=false;
	}
	else if(backup_running==RUNNING_INCR_FILE)
	{
		return "INCR";
	}
	else if(backup_running==RUNNING_FULL_FILE)
	{
		return "FULL";
	}
	else if(backup_running==RUNNING_FULL_IMAGE)
	{
		return "FULLI";
	}
	else if(backup_running==RUNNING_INCR_IMAGE)
	{
		return "INCRI";
	}
	else if(backup_running==RUNNING_RESUME_INCR_FILE)
	{
		return "R_INCR";
	}
	else if(backup_running==RUNNING_RESUME_FULL_FILE)
	{
		return "R_FULL";
	}

	return std::string();
}

void ClientConnector::CMD_STATUS(const std::string &cmd)
{
	state=CCSTATE_STATUS;

	lasttime=Server->getTimeMS();
}

void ClientConnector::CMD_STATUS_DETAIL(const std::string &cmd)
{
	IScopedLock lock(backup_mutex);

	JSON::Object ret;

	ret.set("last_backup_time", getLastBackupTime());

	if(backup_running!=RUNNING_INCR_IMAGE)
		ret.set("percent_done", pcdone);
	else
		ret.set("percent_done", pcdone2);

	ret.set("eta_ms", eta_ms);

	ret.set("currently_running", getCurrRunningJob());

	JSON::Array servers;

	for(size_t i=0;i<channel_pipes.size();++i)
	{
		JSON::Object obj;
		obj.set("internet_connection", channel_pipes[i].internet_connection);
		obj.set("name", channel_pipes[i].endpoint_name);
		servers.add(obj);
	}

	ret.set("servers", servers);

	ret.set("time_since_last_lan_connection", InternetClient::timeSinceLastLanConnection());

	ret.set("internet_connected", InternetClient::isConnected());

	ret.set("internet_status", InternetClient::getStatusMsg());

	ret.set("capability_bits", getCapabilities());

	tcpstack.Send(pipe, ret.get(false));

	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
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
	std::string pcdone_new=getbetween("-","-", cmd);
	if(backup_source_token.empty() || backup_source_token==server_token )
	{
		int pcdone_old=pcdone;

		if(pcdone_new.empty())
			pcdone=-1;
		else
			pcdone=atoi(pcdone_new.c_str());

		if(pcdone_old!=pcdone)
		{
			status_updated = true;
		}

		eta_ms = 0;
	}
	last_token_times[server_token]=Server->getTimeSeconds();

#ifdef _WIN32
	SetThreadExecutionState(ES_SYSTEM_REQUIRED);
#endif
}

void ClientConnector::CMD_PING_RUNNING2(const std::string &cmd)
{
	std::string params_str = cmd.substr(14);
	str_map params;
	ParseParamStrHttp(params_str, &params);
	tcpstack.Send(pipe, "OK");

	IScopedLock lock(backup_mutex);
	lasttime=Server->getTimeMS();
	last_pingtime=Server->getTimeMS();

	if(backup_source_token.empty() || backup_source_token==server_token )
	{
		std::wstring pcdone_new=params[L"pc_done"];

		int pcdone_old = pcdone;

		if(pcdone_new.empty())
			pcdone=-1;
		else
			pcdone=watoi(pcdone_new);

		if(pcdone_old!=pcdone)
		{
			status_updated = true;
		}

		eta_ms = watoi64(params[L"eta_ms"]);
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

		int capa=0;
		std::string token;
		if(cmd.find("1CHANNEL ")==0)
		{
			std::string s_params=cmd.substr(9);
			str_map params;
			ParseParamStrHttp(s_params, &params);
			capa=watoi(params[L"capa"]);
			token=wnarrow(params[L"token"]);
		}
		channel_capa.push_back(capa);

		channel_pipe=SChannel(pipe, internet_conn, endpoint_name, token, &make_fileserv);
		channel_pipes.push_back(SChannel(pipe, internet_conn, endpoint_name, token, &make_fileserv));
		is_channel=true;
		state=CCSTATE_CHANNEL;
		last_channel_ping=Server->getTimeMS();
		lasttime=Server->getTimeMS();
		Server->Log("New channel: Number of Channels: "+nconvert((int)channel_pipes.size()), LL_DEBUG);
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
	if(last_capa & DONT_SHOW_SETTINGS)
	{
		tcpstack.Send(pipe, "FAILED");
		return;
	}

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
	if(ident_ok)
	{
		lasttime=Server->getTimeMS();
		std::string s_params=cmd.substr(11);
		str_map params;
		ParseParamStrHttp(s_params, &params);

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
		image_inf.bitmap=false;
		if(params.find(L"bitmap")!=params.end())
		{
			if(params[L"bitmap"]==L"1")
				image_inf.with_bitmap=true;
		}

		image_inf.with_emptyblocks=false;

		image_inf.no_shadowcopy=false;

		if(image_inf.image_letter=="SYSVOL"
			|| image_inf.image_letter=="ESP")
		{
			std::wstring mpath;
			std::wstring sysvol;
			if(image_inf.image_letter=="SYSVOL")
			{
				sysvol=getSysVolume(mpath);
			}
			else
			{
				sysvol=getEspVolume(mpath);
			}
			
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
			mempipe_owner=false;
		}
		else if(image_inf.shadow_id!=-1)
		{
			image_inf.shadowdrive.clear();
			CWData data;
			data.addChar(4);
			data.addVoidPtr(mempipe);
			data.addInt(image_inf.shadow_id);
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			mempipe_owner=false;
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
	if(ident_ok)
	{
		lasttime=Server->getTimeMS();
		std::string s_params=cmd.substr(11);
		str_map params;
		ParseParamStrHttp(s_params, &params);

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
			image_inf.with_bitmap=false;
			if(params.find(L"bitmap")!=params.end())
			{
				if(params[L"bitmap"]==L"1")
					image_inf.with_bitmap=true;
			}
			



			image_inf.with_emptyblocks=false;
			if(params.find(L"emptyblocks")!=params.end())
			{
				if(params[L"emptyblocks"]==L"1")
					image_inf.with_emptyblocks=true;
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
				mempipe_owner=false;
			}
			else if(image_inf.shadow_id!=-1)
			{
				image_inf.shadowdrive.clear();
				CWData data;
				data.addChar(4);
				data.addVoidPtr(mempipe);
				data.addInt(image_inf.shadow_id);
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
				mempipe_owner=false;
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
	}
	else
	{
		ImageErr("Ident reset (2)");
	}
}

void ClientConnector::CMD_MBR(const std::string &cmd)
{
	lasttime=Server->getTimeMS();
	std::string s_params=cmd.substr(4);
	str_map params;
	ParseParamStrHttp(s_params, &params);

	std::wstring dl=params[L"driveletter"];

	if(dl==L"SYSVOL")
	{
		std::wstring mpath;
		dl=getSysVolume(mpath);
	}
	else if(dl==L"ESP")
	{
		std::wstring mpath;
		dl=getEspVolume(mpath);
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
            sendChannelPacket(channel_pipes[i], "GET BACKUPCLIENTS");
			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);
            std::string nc=receivePacket(channel_pipes[i]);
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
            sendChannelPacket(channel_pipes[i], cmd);
            std::string nc=receivePacket(channel_pipes[i]);
			if(!nc.empty())
			{
				imgs+=nc+"\n";
			}
		}
		tcpstack.Send(pipe, "1"+imgs);
	}
}

void ClientConnector::CMD_RESTORE_GET_FILE_BACKUPS(const std::string &cmd)
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
		std::string filebackups;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
            sendChannelPacket(channel_pipes[i], cmd);
            std::string nc=receivePacket(channel_pipes[i]);
			if(!nc.empty())
			{
				filebackups+=nc+"\n";
			}
		}
		tcpstack.Send(pipe, "1"+filebackups);
	}
}


void ClientConnector::CMD_RESTORE_GET_FILE_BACKUPS_TOKENS( const std::string &cmd, str_map &params )
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
		std::string accessparams = getAccessTokensParams(params[L"tokens"], true);

		if(accessparams.empty())
		{
			tcpstack.Send(pipe, "0");
			return;
		}

		accessparams[0]=' ';

		std::string filebackups;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
            sendChannelPacket(channel_pipes[i], cmd+accessparams);
            std::string nc=receivePacket(channel_pipes[i]);
            if(!nc.empty() && nc!="err")
			{
				if(!filebackups.empty())
				{
					filebackups[filebackups.size()-1] = ',';
					nc[0]=' ';
				}
				filebackups+=nc;
			}
		}
        if(filebackups.empty())
        {
            tcpstack.Send(pipe, "0");
        }
        else
        {
            tcpstack.Send(pipe, "1"+filebackups);
        }
	}
}

void ClientConnector::CMD_GET_FILE_LIST_TOKENS(const std::string &cmd, str_map &params)
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
		std::string accessparams = getAccessTokensParams(params[L"tokens"], true);

		if(accessparams.empty())
		{
			tcpstack.Send(pipe, "0");
			return;
		}

		accessparams[0]=' ';

		str_map::iterator it_path = params.find(L"path");

		if(it_path!=params.end())
		{
			accessparams+="&path="+EscapeParamString(Server->ConvertToUTF8(it_path->second));
		}

		str_map::iterator it_backupid = params.find(L"backupid");

		if(it_backupid!=params.end())
		{
			accessparams+="&backupid="+EscapeParamString(Server->ConvertToUTF8(it_backupid->second));
		}

		for(size_t i=0;i<channel_pipes.size();++i)
		{
            sendChannelPacket(channel_pipes[i], cmd+accessparams);

            std::string nc=receivePacket(channel_pipes[i]);
			if(!nc.empty() && nc!="err")
			{
				tcpstack.Send(pipe, "1"+nc);
				return;
			}
		}

		tcpstack.Send(pipe, "0");
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

void ClientConnector::CMD_RESTORE_DOWNLOAD_FILES(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	Server->Log("Downloading image...", LL_DEBUG);
	IScopedLock lock(backup_mutex);
	waitForPings(&lock);
	
	if(channel_pipes.size()==0)
	{
		tcpstack.Send(pipe, "No backup server found");
		return;
	}

	std::string last_error;

	for(size_t i=0;i<channel_pipes.size();++i)
	{
		IPipe *c=channel_pipes[i].pipe;

		tcpstack.Send(c, "DOWNLOAD FILES backupid="+wnarrow(params[L"backupid"])+"&time="+wnarrow(params[L"time"]));

		Server->Log("Start downloading files from channel "+nconvert((int)i), LL_DEBUG);

		CTCPStack recv_stack;

		int64 starttime = Server->getTimeMS();

		while(Server->getTimeMS()-starttime<60000)
		{
			std::string msg;
			if(c->Read(&msg, 60000)>0)
			{
				recv_stack.AddData(&msg[0], msg.size());

				if(recv_stack.getPacket(msg) )
				{
					if(msg=="ok")
					{
						tcpstack.Send(pipe, "ok");
						return;
					}
					else
					{
						last_error=msg;
					}

					break;
				}
			}
			else
			{
				break;
			}
		}
	}

	if(!last_error.empty())
	{
		tcpstack.Send(pipe, last_error);
	}
	else
	{
		tcpstack.Send(pipe, "timeout");
	}
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
			if(params[L"username"].empty())
			{
                sendChannelPacket(channel_pipes[i], "LOGIN username=&password=");
			}
			else
			{
                sendChannelPacket(channel_pipes[i], "LOGIN username="+Server->ConvertToUTF8(params[L"username"])
														+"&password="+Server->ConvertToUTF8(params[L"password"+convert(i)]));
			}

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);

            std::string nc=receivePacket(channel_pipes[i]);

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

void ClientConnector::CMD_RESTORE_GET_SALT(const std::string &cmd, str_map &params)
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
		std::string salts;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
            sendChannelPacket(channel_pipes[i], "SALT username="+Server->ConvertToUTF8(params[L"username"]));

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);

            std::string nc=receivePacket(channel_pipes[i]);

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after read -1", LL_DEBUG);

			Server->Log("Client "+nconvert(i)+"/"+nconvert(channel_pipes.size())+": --"+nc+"--", LL_DEBUG);

			if(nc.find("ok;")==0)
			{
				if(!salts.empty())
				{
					salts+="/";
				}
				salts+=nc;
			}
			else
			{
				Server->Log("Client "+nconvert(i)+"/"+nconvert(channel_pipes.size())+" ERROR: --"+nc+"--", LL_ERROR);
				salts+="err;"+nc;
			}
		}
		tcpstack.Send(pipe, salts);
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
		ParseParamStrHttp(cmd.substr(14), &params);

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
	std::wstring client_version_str=std::wstring(c_client_version);
#ifdef _WIN32
	std::wstring buf;
	buf.resize(1024);
	std::string os_version_str = get_windows_version();
	std::string win_volumes;
	std::string win_nonusb_volumes;

	{
		IScopedLock lock(backup_mutex);
		win_volumes = get_all_volumes_list(false, volumes_cache);
		win_nonusb_volumes = get_all_volumes_list(true, volumes_cache);
	}

	tcpstack.Send(pipe, "FILE=2&FILE2=1&IMAGE=1&UPDATE=1&MBR=1&FILESRV=3&SET_SETTINGS=1&IMAGE_VER=1&CLIENTUPDATE=1"
		"&CLIENT_VERSION_STR="+EscapeParamString(Server->ConvertToUTF8(client_version_str))+"&OS_VERSION_STR="+EscapeParamString(os_version_str)+
		"&ALL_VOLUMES="+EscapeParamString(win_volumes)+"&ETA=1&CDP=0&ALL_NONUSB_VOLUMES="+EscapeParamString(win_nonusb_volumes)+"&EFI=1"
		"&FILE_META=1");
#else
	std::string os_version_str=get_lin_os_version();
	tcpstack.Send(pipe, "FILE=2&FILE2=1&FILESRV=3&SET_SETTINGS=1&CLIENTUPDATE=1"
		"&CLIENT_VERSION_STR="+EscapeParamString(Server->ConvertToUTF8(client_version_str))+"&OS_VERSION_STR="+EscapeParamString(os_version_str)
		+"&ETA=1&CPD=0&FILE_META=1");
#endif
}

void ClientConnector::CMD_NEW_SERVER(str_map &params)
{
	std::string ident=Server->ConvertToUTF8(params[L"ident"]);
	if(!ident.empty())
	{
		ServerIdentityMgr::addServerIdentity(ident, "");
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
	IPipe* localpipe=Server->createMemoryPipe();
	data.addChar(IndexThread::IndexThreadAction_GetLog);
	data.addVoidPtr(localpipe);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	std::string ret;
	localpipe->Read(&ret, 8000);
	tcpstack.Send(pipe, ret);
	localpipe->Write("exit");
}

void ClientConnector::CMD_GET_ACCESS_PARAMS(str_map &params)
{
	if(crypto_fak==NULL)
	{
		Server->Log("No cryptoplugin present. Action not possible.", LL_ERROR);
		tcpstack.Send(pipe, "");
		return;
	}

	std::wstring tokens=params[L"tokens"];

	std::auto_ptr<ISettingsReader> settings(
		Server->createFileSettingsReader("urbackup/data/settings.cfg"));

	std::string server_url;
	if( (!settings->getValue("server_url", &server_url)
		&& !settings->getValue("server_url_def", &server_url) ) 
		|| server_url.empty())
	{
		Server->Log("Server url empty", LL_ERROR);
		tcpstack.Send(pipe, "");
		return;
	}

	std::string ret = getAccessTokensParams(tokens, true);

	if(!ret.empty())
	{
		ret[0]='#';
		ret = server_url + ret;
	}
	else
	{
		tcpstack.Send(pipe, "");
		return;
	}

	tcpstack.Send(pipe, ret);
}

void ClientConnector::CMD_CONTINUOUS_WATCH_START()
{
	IScopedLock lock(backup_mutex);

	tcpstack.Send(pipe, "OK");
}

void ClientConnector::CMD_SCRIPT_STDERR(const std::string& cmd)
{
	std::wstring script_cmd = Server->ConvertToUnicode(cmd.substr(14));

	if(next(script_cmd, 0, L"SCRIPT|"))
	{
		script_cmd = script_cmd.substr(7);
	}

	std::string stderr_out;
	int exit_code;
	if(IndexThread::getFileSrv()->getExitInformation(script_cmd, stderr_out, exit_code))
	{
		tcpstack.Send(pipe, nconvert(exit_code)+" "+stderr_out);
	}
	else
	{
		tcpstack.Send(pipe, "err");
	}
}

void ClientConnector::CMD_FILE_RESTORE(const std::string& cmd)
{
	str_map params;
	ParseParamStrHttp(cmd, &params);

	std::string client_token = Server->ConvertToUTF8(params[L"client_token"]);
	std::string server_token=wnarrow(params[L"server_token"]);
	int64 restore_id=watoi64(params[L"id"]);
	int64 status_id=watoi64(params[L"status_id"]);
	int64 log_id=watoi64(params[L"log_id"]);

	{
		IScopedLock lock(backup_mutex);
		restore_ok_status = RestoreOk_Wait;
	}

	Server->getThreadPool()->execute(new RestoreFiles(restore_id, status_id, log_id, client_token, server_token));

	tcpstack.Send(pipe, "ok");
}

void ClientConnector::CMD_RESTORE_OK( str_map &params )
{
	IScopedLock lock(backup_mutex);
	if(params[L"ok"]==L"true")
	{
		restore_ok_status = RestoreOk_Ok;
	}
	else
	{
		restore_ok_status = RestoreOk_Declined;
	}

	tcpstack.Send(pipe, "ok");
}
