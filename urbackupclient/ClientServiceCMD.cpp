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
std::string getSysVolumeCached(std::string &mpath){ return ""; }
std::string getEspVolume(std::string &mpath){ return ""; }
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
			ServerIdentityMgr::addServerIdentity(identity, SPublicKeys());
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
				ServerIdentityMgr::addServerIdentity(identity, SPublicKeys());
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
	std::string challenge = Server->secureRandomString(30)+"-"+convert(Server->getTimeSeconds())+"-"+convert(Server->getTimeMS());
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

	str_map::iterator challenge_it = challenges.find(identity);

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

	std::string pubkey = base64_decode_dash(params["pubkey"]);
	std::string pubkey_ecdsa409k1 = base64_decode_dash(params["pubkey_ecdsa409k1"]);
	std::string signature = base64_decode_dash(params["signature"]);
	std::string signature_ecdsa409k1 = base64_decode_dash(params["signature_ecdsa409k1"]);
	std::string session_identity = params["session_identity"];

	if(!ServerIdentityMgr::hasPublicKey(identity))
	{
		ServerIdentityMgr::setPublicKeys(identity, SPublicKeys(pubkey, pubkey_ecdsa409k1));
	}

	SPublicKeys pubkeys = ServerIdentityMgr::getPublicKeys(identity);
	
	if( (!pubkeys.ecdsa409k1_key.empty() && crypto_fak->verifyData(pubkeys.ecdsa409k1_key, challenge, signature_ecdsa409k1))
		|| (pubkeys.ecdsa409k1_key.empty() && !pubkeys.dsa_key.empty() && crypto_fak->verifyDataDSA(pubkeys.dsa_key, challenge, signature)) )
	{
		ServerIdentityMgr::addSessionIdentity(session_identity, endpoint_name);
		ServerIdentityMgr::setPublicKeys(identity, SPublicKeys(pubkey, pubkey_ecdsa409k1));
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

	std::string resume = params["resume"];
	std::string sha_version_str = params["sha"];
	int sha_version = 512;
	if(!sha_version_str.empty())
	{
		sha_version = watoi(sha_version_str);
	}

	int group=c_group_default;

	str_map::iterator it_group = params.find("group");
	if(it_group!=params.end())
	{
		group = watoi(it_group->second);
	}

	std::string clientsubname;

	str_map::iterator it_clientsubname = params.find("clientsubname");
	if(it_clientsubname!=params.end())
	{
		clientsubname = conv_filename((it_clientsubname->second));
	}

	unsigned int flags = 0;

	if(params.find("with_scripts")!=params.end())
	{
		flags |= flag_with_scripts;
	}

	if(params.find("with_orig_path")!=params.end())
	{
		flags |= flag_with_orig_path;
	}

	if(params.find("with_sequence")!=params.end())
	{
		flags |= flag_with_sequence;
	}

	if(params.find("with_proper_symlinks")!=params.end())
	{
		flags |= flag_with_proper_symlinks;
	}

	if(end_to_end_file_backup_verification_enabled)
	{
		flags |= flag_end_to_end_verification;
	}

	if(calculateFilehashesOnClient(clientsubname))
	{
		flags |= flag_calc_checksums;
	}

	state=CCSTATE_START_FILEBACKUP;

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_StartIncrFileBackup);
	data.addVoidPtr(mempipe);
	data.addString(server_token);
	data.addInt(group);
	data.addInt(flags);
	data.addString(clientsubname);
	data.addInt(sha_version);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
	mempipe_owner=false;

	lasttime=Server->getTimeMS();

	if(resume.empty())
	{
		backup_running=RUNNING_INCR_FILE;
	}
	else if(resume=="full")
	{
		backup_running=RUNNING_RESUME_FULL_FILE;
	}
	else if(resume=="incr")
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

	str_map::iterator it_group = params.find("group");
	if(it_group!=params.end())
	{
		group = watoi(it_group->second);
	}

	std::string sha_version_str = params["sha"];
	int sha_version = 512;
	if(!sha_version_str.empty())
	{
		sha_version = watoi(sha_version_str);
	}
	std::string clientsubname;
	str_map::iterator it_clientsubname = params.find("clientsubname");
	if(it_clientsubname!=params.end())
	{
		clientsubname = conv_filename((it_clientsubname->second));
	}

	int flags = 0;

	if(params.find("with_scripts")!=params.end())
	{
		flags |= flag_with_scripts;
	}

	if(params.find("with_orig_path")!=params.end())
	{
		flags |= flag_with_orig_path;
	}

	if(params.find("with_sequence")!=params.end())
	{
		flags |= flag_with_sequence;
	}

	if(params.find("with_proper_symlinks")!=params.end())
	{
		flags |= flag_with_proper_symlinks;
	}

	if(end_to_end_file_backup_verification_enabled)
	{
		flags |= flag_end_to_end_verification;
	}

	if(calculateFilehashesOnClient(clientsubname))
	{
		flags |= flag_calc_checksums;
	}

	state=CCSTATE_START_FILEBACKUP;

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_StartFullFileBackup);
	data.addVoidPtr(mempipe);
	data.addString(server_token);
	data.addInt(group);
	data.addInt(flags);
	data.addString(clientsubname);
	data.addInt(sha_version);
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
}

void ClientConnector::CMD_STOP_SHADOWCOPY(const std::string &cmd)
{
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
}

void ClientConnector::CMD_SET_INCRINTERVAL(const std::string &cmd)
{
	if(cmd[cmd.size()-1]=='"')
	{
		IScopedLock lock(backup_mutex);

		std::string intervall=cmd.substr(15, cmd.size()-16);

		bool update_db=false;

		if(intervall.find("?")!=std::string::npos)
		{
			str_map params;
			ParseParamStrHttp(getafter("?", intervall), &params);

			str_map::iterator it_startup_delay=params.find("startup_delay");
			if(it_startup_delay!=params.end())
			{
				int new_waittime = watoi(it_startup_delay->second);
				if(new_waittime>backup_alert_delay)
				{
					update_db=true;					
					backup_alert_delay = new_waittime;
				}
			}
		}

		int new_interval=atoi(intervall.c_str());
		if(backup_interval==0 || new_interval<backup_interval)
		{			
			update_db=true;
			backup_interval = new_interval;
		}
		tcpstack.Send(pipe, "OK");
		lasttime=Server->getTimeMS();

		if(update_db)
		{
			IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
			ClientDAO dao(db);
			dao.updateMiscValue("backup_interval", convert(backup_interval));
			dao.updateMiscValue("backup_alert_delay", convert(backup_alert_delay));
		}
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
			if(res[i]["name"]=="*") continue;

			JSON::Object cdir;

			cdir.set("id", watoi(res[i]["id"]));
			cdir.set("name", res[i]["name"]);
			cdir.set("path", res[i]["path"]);
			cdir.set("group", watoi(res[i]["tgroup"]));

			dirs.add(cdir);
		}

		ret.set("dirs", dirs);

        tcpstack.Send(pipe, ret.stringify(true));
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
			backup_running_owner=NULL;
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
	IQuery *q=db->Prepare("SELECT strftime('%s',last_backup) AS last_backup FROM status");
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
		return (res[0]["last_backup"]);
	}
	else
	{
		return std::string();
	}
}

std::string ClientConnector::getCurrRunningJob(bool reset_done)
{
	if(last_pingtime!=0 && Server->getTimeMS()-last_pingtime>x_pingtimeout)
	{
		return getHasNoRecentBackup();
	}

	if(backup_running==RUNNING_NONE)
	{
		if(backup_done)
		{
			return "DONE";
		}
		else
		{
			return getHasNoRecentBackup();
		}
		
		if(reset_done)
		{
			backup_done=false;
		}
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

	ret.set("currently_running", getCurrRunningJob(false));

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

    tcpstack.Send(pipe, ret.stringify(false));

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
		std::string pcdone_new=params["pc_done"];

		int pcdone_old = pcdone;

		if(pcdone_new.empty())
			pcdone=-1;
		else
			pcdone=watoi(pcdone_new);

		if(pcdone_old!=pcdone)
		{
			status_updated = true;
		}

		eta_ms = watoi64(params["eta_ms"]);
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

		std::string token;

		std::string s_params=cmd.substr(9);
		str_map params;
		ParseParamStrHttp(s_params, &params);
		int capa=watoi(params["capa"]);
		token=params["token"];

		channel_capa.push_back(capa);

		channel_pipe=SChannel(pipe, internet_conn, endpoint_name, token, &make_fileserv);
		channel_pipes.push_back(SChannel(pipe, internet_conn, endpoint_name, token, &make_fileserv));
		is_channel=true;
		state=CCSTATE_CHANNEL;
		last_channel_ping=Server->getTimeMS();
		lasttime=Server->getTimeMS();
		Server->Log("New channel: Number of Channels: "+convert((int)channel_pipes.size()), LL_DEBUG);
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
	int logid=watoi(params["logid"]);
	int loglevel=watoi(params["loglevel"]);
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

		server_token=(params["token"]);
		image_inf.image_letter=(params["letter"]);
		image_inf.shadowdrive=(params["shadowdrive"]);
		if(params.find("start")!=params.end())
		{
			image_inf.startpos=(uint64)_atoi64((params["start"]).c_str());
		}
		else
		{
			image_inf.startpos=0;
		}
		if(params.find("shadowid")!=params.end())
		{
			image_inf.shadow_id=watoi(params["shadowid"]);
		}
		else
		{
			image_inf.shadow_id=-1;
		}
		image_inf.with_checksum=false;
		if(params.find("checksum")!=params.end())
		{
			if(params["checksum"]=="1")
				image_inf.with_checksum=true;
		}
		image_inf.with_bitmap=false;
		if(params.find("bitmap")!=params.end())
		{
			if(params["bitmap"]=="1")
				image_inf.with_bitmap=true;
		}

		image_inf.with_emptyblocks=false;

		image_inf.no_shadowcopy=false;

		if(image_inf.image_letter=="SYSVO"
			|| image_inf.image_letter=="ESP")
		{
			std::string mpath;
			std::string sysvol;
			if(image_inf.image_letter=="SYSVO")
			{
				sysvol=getSysVolumeCached(mpath);
			}
			else
			{
				sysvol=getEspVolume(mpath);
			}
			
			if(!mpath.empty())
			{
				image_inf.image_letter=(mpath);
			}
			else
			{
				image_inf.image_letter=(sysvol);
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

		server_token=(params["token"]);

		str_map::iterator f_hashsize=params.find("hashsize");
		if(f_hashsize!=params.end())
		{
			hashdataok=false;
			hashdataleft=watoi(f_hashsize->second);
			image_inf.image_letter=(params["letter"]);
			image_inf.shadowdrive=(params["shadowdrive"]);
			if(params.find("start")!=params.end())
			{
				image_inf.startpos=(uint64)_atoi64((params["start"]).c_str());
			}
			else
			{
				image_inf.startpos=0;
			}
			if(params.find("shadowid")!=params.end())
			{
				image_inf.shadow_id=watoi(params["shadowid"]);
			}
			else
			{
				image_inf.shadow_id=-1;
			}
			image_inf.with_checksum=false;
			if(params.find("checksum")!=params.end())
			{
				if(params["checksum"]=="1")
					image_inf.with_checksum=true;
			}
			image_inf.with_bitmap=false;
			if(params.find("bitmap")!=params.end())
			{
				if(params["bitmap"]=="1")
					image_inf.with_bitmap=true;
			}
			



			image_inf.with_emptyblocks=false;
			if(params.find("emptyblocks")!=params.end())
			{
				if(params["emptyblocks"]=="1")
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
					//Server->Log("Hashdataleft: "+convert(hashdataleft), LL_DEBUG);
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

	std::string dl=params["driveletter"];

	if(dl=="SYSVO")
	{
		std::string mpath;
		dl=getSysVolumeCached(mpath);
	}
	else if(dl=="ESP")
	{
		std::string mpath;
		dl=getEspVolume(mpath);
	}

	bool b=false;
	std::string errmsg;
	if(!dl.empty())
	{
		b=sendMBR(dl, errmsg);
	}
	if(!b)
	{
		CWData r;
		r.addChar(0);
		r.addString((errmsg));
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
						
			Server->Log("Client "+convert(i)+"/"+convert(channel_pipes.size())+": --"+nc+"--", LL_DEBUG);
					
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

		std::string utf8_tokens = (params["tokens"]);
		std::string filebackups;
		std::string accessparams;

		if(channel_pipes.size()==1)
		{
			accessparams+=" with_id_offset=false";
		}

		bool has_token_params=false;

		for(size_t i=0;i<channel_pipes.size();++i)
		{
			for(size_t j=0;j<2;++j)
			{
				if(channel_pipes[i].last_tokens!=utf8_tokens)
				{
					if(!has_token_params)
					{
						std::string token_params = getAccessTokensParams(params["tokens"], true);

						if(token_params.empty())
						{
							tcpstack.Send(pipe, "0");
							return;
						}

						if(accessparams.empty())
						{
							accessparams = token_params;
							accessparams[0]=' ';
						}
						else
						{
							accessparams+=token_params;
						}

						has_token_params=true;
					}			
				}

				sendChannelPacket(channel_pipes[i], cmd+accessparams);
				std::string nc=receivePacket(channel_pipes[i]);
				if(!nc.empty() && nc!="err")
				{
					channel_pipes[i].last_tokens = utf8_tokens;

					if(!filebackups.empty())
					{
						filebackups[filebackups.size()-1] = ',';
						nc[0]=' ';
					}
					filebackups+=nc;

					break;
				}
				else if(!has_token_params)
				{
					channel_pipes[i].last_tokens="";
				}
				else
				{
					break;
				}
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
		std::string accessparams;

		str_map::iterator it_path = params.find("path");

		if(it_path!=params.end())
		{
			accessparams+="&path="+EscapeParamString((it_path->second));
		}

		str_map::iterator it_backupid = params.find("backupid");

		if(it_backupid!=params.end())
		{
			accessparams+="&backupid="+EscapeParamString((it_backupid->second));
		}

		if(channel_pipes.size()==1)
		{
			accessparams+="&with_id_offset=false";
		}

        std::string filelist;

		if(!accessparams.empty())
		{
			accessparams[0]=' ';
		}

		std::string utf8_tokens = (params["tokens"]);
		bool has_token_params=false;
		bool break_outer=false;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
			for(size_t j=0;j<2;++j)
			{
				if(channel_pipes[i].last_tokens!=utf8_tokens)
				{
					if(!has_token_params)
					{
						std::string token_params = getAccessTokensParams(params["tokens"], true);

						if(token_params.empty())
						{
							tcpstack.Send(pipe, "0");
							return;
						}

						if(accessparams.empty())
						{
							accessparams = token_params;
							accessparams[0]=' ';
						}
						else
						{
							accessparams+=token_params;
						}

						has_token_params=true;
					}
				}

				sendChannelPacket(channel_pipes[i], cmd+accessparams);

				std::string nc=receivePacket(channel_pipes[i]);
				if(!nc.empty() && nc!="err")
				{
					channel_pipes[i].last_tokens = utf8_tokens;

					if(!filelist.empty())
					{
						filelist[filelist.size()-1] = ',';
						nc[0]=' ';
					}

					filelist+=nc;

					if(it_backupid!=params.end())
					{
						break_outer=true;
						break;
					}

					break;
				}
				else if(!has_token_params)
				{
					channel_pipes[i].last_tokens="";
				}
				else
				{
					break;
				}
			}

			if(break_outer)
			{
				break;
			}
		}

        if(!filelist.empty())
        {
            tcpstack.Send(pipe, "1"+filelist);
        }
        else
        {
            tcpstack.Send(pipe, "0");
        }
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

		tcpstack.Send(c, "DOWNLOAD FILES backupid="+params["backupid"]+"&time="+params["time"]);

		Server->Log("Start downloading files from channel "+convert((int)i), LL_DEBUG);

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
				if(!pipe->Write(convert(progress)+"\n", 10000))
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
			if(params["username"].empty())
			{
                sendChannelPacket(channel_pipes[i], "LOGIN username=&password=");
			}
			else
			{
                sendChannelPacket(channel_pipes[i], "LOGIN username="+(params["username"])
														+"&password="+(params["password"+convert(i)]));
			}

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);

            std::string nc=receivePacket(channel_pipes[i]);

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after read -1", LL_DEBUG);

			Server->Log("Client "+convert(i)+"/"+convert(channel_pipes.size())+": --"+nc+"--", LL_DEBUG);

			if(nc=="ok")
			{
				one_ok=true;
			}
			else
			{
				Server->Log("Client "+convert(i)+"/"+convert(channel_pipes.size())+" ERROR: --"+nc+"--", LL_ERROR);
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
            sendChannelPacket(channel_pipes[i], "SALT username="+(params["username"]));

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after request -1", LL_DEBUG);

            std::string nc=receivePacket(channel_pipes[i]);

			if(channel_pipes[i].pipe->hasError())
				Server->Log("Channel has error after read -1", LL_DEBUG);

			Server->Log("Client "+convert(i)+"/"+convert(channel_pipes.size())+": --"+nc+"--", LL_DEBUG);

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
				Server->Log("Client "+convert(i)+"/"+convert(channel_pipes.size())+" ERROR: --"+nc+"--", LL_ERROR);
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

		hashdataleft=watoi(params["size"]);
		silent_update=(params["silent_update"]=="true");
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
	std::string buf;
	buf.resize(1024);
	std::string os_version_str = get_windows_version();
	std::string win_volumes;
	std::string win_nonusb_volumes;

	{
		IScopedLock lock(backup_mutex);
		win_volumes = get_all_volumes_list(false, volumes_cache);
		win_nonusb_volumes = get_all_volumes_list(true, volumes_cache);
	}

	std::string restore=Server->getServerParameter("allow_restore");

	if(restore.empty())
	{
		restore="client-confirms";
	}

	tcpstack.Send(pipe, "FILE=2&FILE2=1&IMAGE=1&UPDATE=1&MBR=1&FILESRV=3&SET_SETTINGS=1&IMAGE_VER=1&CLIENTUPDATE=1"
		"&CLIENT_VERSION_STR="+EscapeParamString((client_version_str))+"&OS_VERSION_STR="+EscapeParamString(os_version_str)+
		"&ALL_VOLUMES="+EscapeParamString(win_volumes)+"&ETA=1&CDP=0&ALL_NONUSB_VOLUMES="+EscapeParamString(win_nonusb_volumes)+"&EFI=1"
		"&FILE_META=1&SELECT_SHA=1&RESTORE="+restore);
#else
	std::string os_version_str=get_lin_os_version();
	tcpstack.Send(pipe, "FILE=2&FILE2=1&FILESRV=3&SET_SETTINGS=1&CLIENTUPDATE=1"
		"&CLIENT_VERSION_STR="+EscapeParamString((client_version_str))+"&OS_VERSION_STR="+EscapeParamString(os_version_str)
		+"&ETA=1&CPD=0&FILE_META=1&SELECT_SHA=1&RESTORE="+restore);
#endif
}

void ClientConnector::CMD_NEW_SERVER(str_map &params)
{
	std::string ident=(params["ident"]);
	if(!ident.empty())
	{
		ServerIdentityMgr::addServerIdentity(ident, SPublicKeys());
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

	std::string tokens=params["tokens"];

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
	std::string script_cmd = (cmd.substr(14));

	if(next(script_cmd, 0, "SCRIPT|"))
	{
		script_cmd = script_cmd.substr(7);
	}

	std::string stderr_out;
	int exit_code;
	if(IndexThread::getFileSrv()->getExitInformation(script_cmd, stderr_out, exit_code))
	{
		tcpstack.Send(pipe, convert(exit_code)+" "+stderr_out);
	}
	else
	{
		tcpstack.Send(pipe, "err");
	}
}

void ClientConnector::CMD_FILE_RESTORE(const std::string& cmd)
{
	std::string restore=Server->getServerParameter("allow_restore");
	if(restore!="client-confirms" && restore!="server-confirms")
	{
		tcpstack.Send(pipe, "disabled");
		return;
	}

	str_map params;
	ParseParamStrHttp(cmd, &params);

	std::string client_token = (params["client_token"]);
	std::string server_token=params["server_token"];
	int64 restore_id=watoi64(params["id"]);
	int64 status_id=watoi64(params["status_id"]);
	int64 log_id=watoi64(params["log_id"]);

	RestoreFiles* local_restore_files = new RestoreFiles(restore_id, status_id, log_id, client_token, server_token);

	if(restore == "client-confirms")
	{
		IScopedLock lock(backup_mutex);
		restore_ok_status = RestoreOk_Wait;
		status_updated = true;

		if(restore_files!=NULL)
		{
			delete restore_files;
		}

		restore_files = local_restore_files;
	}
	else
	{
		Server->getThreadPool()->execute(local_restore_files);
	}

	tcpstack.Send(pipe, "ok");
}

void ClientConnector::CMD_RESTORE_OK( str_map &params )
{
	IScopedLock lock(backup_mutex);
	if(params["ok"]=="true")
	{
		restore_ok_status = RestoreOk_Ok;

		if(restore_files!=NULL)
		{
			Server->getThreadPool()->execute(restore_files);
			restore_files=NULL;
		}
	}
	else
	{
		restore_ok_status = RestoreOk_Declined;
	}

	if(restore_files!=NULL)
	{
		delete restore_files;
		restore_files=NULL;
	}

	tcpstack.Send(pipe, "ok");
}
