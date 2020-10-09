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
#include "file_permissions.h"
#ifdef _WIN32
#include "win_sysvol.h"
#include "win_ver.h"
#include "win_all_volumes.h"
#include "DirectoryWatcherThread.h"
#include "win_network_cost.h"
#else
#include "lin_ver.h"
#include "lin_sysvol.h"
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
				if(ServerIdentityMgr::hasPublicKey(identity, true))
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
				Server->Log("Rejected server identity " + identity, LL_INFO);
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

void ClientConnector::CMD_ADD_IDENTITY(const std::string & params)
{
	str_map m_params;
	ParseParamStrHttp(params, &m_params);

	ServerIdentityMgr::loadServerIdentities();
	std::string identity = m_params["identity"];
	if (!ServerIdentityMgr::checkServerIdentity(identity))
	{
		ServerIdentityMgr::addServerIdentity(identity, SPublicKeys());
	}
	tcpstack.Send(pipe, "OK");
}

void ClientConnector::CMD_GET_CHALLENGE(const std::string &identity, const std::string& cmd)
{
	if(identity.empty())
	{
		tcpstack.Send(pipe, "");
		return;
	}

	std::string clientsubname;
	bool with_enc = false;
	std::string ecies_pubkey;
	if (cmd.size() > 14)
	{
		std::string s_params = cmd.substr(14);
		str_map params;
		ParseParamStrHttp(s_params, &params);

		clientsubname = params["clientsubname"];
		ecies_pubkey = base64_decode_dash(params["ecies_pubkey"]);
		with_enc = params["with_enc"] == "1";
	}

	bool local_encrypted = false;
	bool local_compressed = false;
	IECDHKeyExchange* shared_key_exchange = NULL;
	std::string ret_params;

	if (!internet_conn)
	{
		std::auto_ptr<ISettingsReader> settings(
			Server->createFileSettingsReader("urbackup/data/settings.cfg"));

		local_encrypted = settings->getValue("local_encrypted", true);
		local_compressed = settings->getValue("local_compressed", true);
	}

	if (with_enc)
	{
		if (local_compressed)
		{
#ifndef NO_ZSTD_COMPRESSION
			ret_params += "&compress=zstd";
#else
			ret_params += "&compress=zstd";
#endif
		}

		if (local_encrypted)
		{
			shared_key_exchange = crypto_fak->createECDHKeyExchange();
			ret_params += "&pubkey_ecdh233k1=" + base64_encode_dash(shared_key_exchange->getPublicKey());
		}

		if (!ret_params.empty())
			ret_params[0] = '?';
	}
	else if (local_encrypted)
	{
		Server->Log("Client requires encryption which server does not offer", LL_ERROR);
		return;
	}

	IScopedLock lock(ident_mutex);
	std::string challenge = Server->secureRandomString(30)+"-"+convert(Server->getTimeSeconds())+"-"+convert(Server->getTimeMS());
	challenges[std::make_pair(identity, clientsubname)]=SChallenge(challenge, shared_key_exchange, local_compressed);

	tcpstack.Send(pipe, challenge + ret_params);
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

	size_t hashpos = cmd.find("#");
	if (hashpos == std::string::npos)
	{
		Server->Log("Signature error: No parameters", LL_ERROR);
		tcpstack.Send(pipe, "no parameters");
		return;
	}

	str_map params;
	ParseParamStrHttp(cmd.substr(hashpos + 1), &params);

	std::string clientsubname = params["clientsubname"];

	IScopedLock lock(ident_mutex);

	std::map<std::pair<std::string, std::string>, SChallenge>::iterator challenge_it = challenges.find(std::make_pair(identity, clientsubname));

	if(challenge_it==challenges.end() || challenge_it->second.challenge_str.empty())
	{
		Server->Log("Signature error: No challenge", LL_ERROR);
		tcpstack.Send(pipe, "no challenge");
		return;
	}

	const std::string& challenge = challenge_it->second.challenge_str;
	

	std::string pubkey = base64_decode_dash(params["pubkey"]);
	std::string pubkey_ecdsa409k1 = base64_decode_dash(params["pubkey_ecdsa409k1"]);
	std::string signature = base64_decode_dash(params["signature"]);
	std::string signature_ecdsa409k1 = base64_decode_dash(params["signature_ecdsa409k1"]);
	std::string session_identity = params["session_identity"];
	std::string pubkey_ecdh233k1 = base64_decode_dash(params["pubkey_ecdh233k1"]);
	std::string signature_ecdh233k1 = base64_decode_dash(params["signature_ecdh233k1"]);
	std::string secret_session_key = base64_decode_dash(params["secret_session_key"]);
	std::string signature_ecies = base64_decode_dash(params["signature_ecies"]);
	std::string ecdh_shared_key;
	std::string secret_session_key_decrypted;

	if(!ServerIdentityMgr::hasPublicKey(identity, false))
	{
		if (!ServerIdentityMgr::setPublicKeys(identity, SPublicKeys(pubkey, pubkey_ecdsa409k1)))
		{
			tcpstack.Send(pipe, "pubkey fingerprint mismatch");
			return;
		}
	}

	SPublicKeys pubkeys = ServerIdentityMgr::getPublicKeys(identity);
	
	if( (!pubkeys.ecdsa409k1_key.empty() && crypto_fak->verifyData(pubkeys.ecdsa409k1_key, challenge, signature_ecdsa409k1))
		|| (pubkeys.ecdsa409k1_key.empty() && !pubkeys.dsa_key.empty() && crypto_fak->verifyDataDSA(pubkeys.dsa_key, challenge, signature)) )
	{
		if (challenge_it->second.shared_key_exchange==NULL ||
			(crypto_fak->verifyData(pubkeys.ecdsa409k1_key, challenge + pubkey_ecdh233k1, signature_ecdh233k1) &&
				!(ecdh_shared_key = challenge_it->second.shared_key_exchange->getSharedKey(pubkey_ecdh233k1)).empty() &&
				!(secret_session_key_decrypted = crypto_fak->decryptAuthenticatedAES(secret_session_key,
					ecdh_shared_key, 1)).empty()
			) )
		{
			if (!ServerIdentityMgr::setPublicKeys(identity, SPublicKeys(pubkey, pubkey_ecdsa409k1)))
			{
				tcpstack.Send(pipe, "pubkey fingerprint mismatch (2)");
				return;
			}

			ServerIdentityMgr::addSessionIdentity(session_identity, endpoint_name,
				secret_session_key_decrypted);
			
			tcpstack.Send(pipe, "ok");
			Server->destroy(challenge_it->second.shared_key_exchange);
			challenges.erase(challenge_it);
		}
		else
		{
			Server->Log("Encryption session key exchange failed", LL_ERROR);
			tcpstack.Send(pipe, "encryption session key exchange failed");
		}		
	}
	else
	{
		Server->Log("Signature error: Verification failed", LL_ERROR);
		tcpstack.Send(pipe, "signature verification failed");
	}
}

void ClientConnector::CMD_START_INCR_FILEBACKUP(const std::string &cmd)
{
	timeoutAsyncFileIndex();

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

	int64 server_id = watoi64(params["status_id"]);
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

	int running_jobs = 2;
	str_map::iterator it_running_jobs = params.find("running_jobs");
	if (it_running_jobs != params.end())
	{
		running_jobs = watoi(it_running_jobs->second);
	}

	bool async_list = params.find("async") != params.end()
		&& params["async"] == "1";

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_StartIncrFileBackup);
	curr_result_id = IndexThread::getResultId();
	data.addUInt(curr_result_id);
	data.addString(server_token);
	data.addInt(group);
	data.addInt(flags);
	data.addString(clientsubname);
	data.addInt(sha_version);
	data.addInt(running_jobs);
	data.addChar(async_list ? 1 : 0);

	std::string async_id;
	if (async_list)
	{
		async_id.resize(16);
		Server->randomFill(&async_id[0], async_id.size());
	}

	if (async_list &&
		(flags & flag_calc_checksums) &&
		params["phash"] == "1")
	{
		data.addString2(async_id);
	}

	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	lasttime=Server->getTimeMS();

	RunningAction action = RUNNING_NONE;
	if(resume.empty())
	{
		action =RUNNING_INCR_FILE;
	}
	else if(resume=="full")
	{
		action =RUNNING_RESUME_FULL_FILE;
	}
	else if(resume=="incr")
	{
		action =RUNNING_RESUME_INCR_FILE;
	}

	SRunningProcess new_proc;

	new_proc.action = action;
	new_proc.server_id = server_id;
	new_proc.id = ++curr_backup_running_id;
	new_proc.server_token = server_token;

	IScopedLock process_lock(process_mutex);

	removeTimedOutProcesses(server_token, true);

	running_processes.push_back(new_proc);

	status_updated = true;

	end_to_end_file_backup_verification_enabled=false;

	if (!async_list)
	{
		state = CCSTATE_START_FILEBACKUP;
	}
	else
	{
		process_lock.relock(NULL);

		SAsyncFileList new_async_file_list = {
			Server->getTimeMS(),
			curr_result_id,
			0
		};

		async_file_index[async_id] = new_async_file_list;

		lock.relock(NULL);

		IndexThread::refResult(curr_result_id);

		tcpstack.Send(pipe, "ASYNC-async_id=" + bytesToHex(async_id));
	}
}

void ClientConnector::CMD_START_FULL_FILEBACKUP(const std::string &cmd)
{
	timeoutAsyncFileIndex();

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

	int64 server_id = watoi64(params["status_id"]);
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

	int running_jobs = 1;
	str_map::iterator it_running_jobs = params.find("running_jobs");
	if (it_running_jobs != params.end())
	{
		running_jobs = watoi(it_running_jobs->second);
	}

	bool async_list = params.find("async") != params.end()
		&& params["async"] == "1";

	IScopedLock lock(backup_mutex);

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_StartFullFileBackup);
	curr_result_id = IndexThread::getResultId();
	data.addUInt(curr_result_id);
	data.addString(server_token);
	data.addInt(group);
	data.addInt(flags);
	data.addString(clientsubname);
	data.addInt(sha_version);
	data.addInt(running_jobs);
	data.addChar(async_list ? 1 : 0);

	std::string async_id;
	if (async_list)
	{
		async_id.resize(16);
		Server->randomFill(&async_id[0], async_id.size());
	}

	if (async_list &&
		(flags & flag_calc_checksums) &&
		params["phash"] == "1")
	{
		data.addString2(async_id);
	}

	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	lasttime=Server->getTimeMS();

	end_to_end_file_backup_verification_enabled=false;

	SRunningProcess new_proc;

	new_proc.action = RUNNING_FULL_FILE;
	new_proc.server_id = server_id;
	new_proc.id = ++curr_backup_running_id;
	new_proc.server_token = server_token;

	IScopedLock process_lock(process_mutex);

	removeTimedOutProcesses(server_token, true);

	running_processes.push_back(new_proc);

	status_updated = true;

	if (!async_list)
	{
		state = CCSTATE_START_FILEBACKUP;
	}
	else
	{
		process_lock.relock(NULL);

		SAsyncFileList new_async_file_list = {
			Server->getTimeMS(),
			curr_result_id,
			0
		};

		async_file_index[async_id] = new_async_file_list;
		Server->Log("Async index " + bytesToHex(async_id), LL_DEBUG);

		lock.relock(NULL);

		IndexThread::refResult(curr_result_id);

		tcpstack.Send(pipe, "ASYNC-async_id="+ bytesToHex(async_id));
	}
}

void ClientConnector::CMD_WAIT_FOR_INDEX(const std::string &cmd)
{
	str_map params;
	ParseParamStrHttp(cmd.substr(15), &params);

	IScopedLock lock(backup_mutex);

	std::string async_id = params["async_id"];
	async_file_list_id = hexToBytes(async_id);
	std::map<std::string, SAsyncFileList>::iterator it = async_file_index.find(async_file_list_id);
	if (it == async_file_index.end())
	{
		for (std::deque<std::pair<std::string, std::string> >::iterator it2 = finished_async_file_index.begin();
			it2 != finished_async_file_index.end(); ++it2)
		{
			if (it2->first == async_file_list_id)
			{
				Server->Log("Previously finished async index " + async_id + " with \"" + it2->second + "\"", LL_DEBUG);
				if (it2->second == "done")
				{
					tcpstack.Send(pipe, "DONE");
				}
				else
				{
					tcpstack.Send(pipe, it2->second);
				}

				return;
			}
		}

		lock.relock(NULL);

		Server->Log("Async index " + async_id + " not found", LL_DEBUG);

		tcpstack.Send(pipe, "error - Async indexing process not found");
	}
	else if (Server->getTimeMS() - it->second.last_update > async_index_timeout)
	{
		unsigned int result_id = it->second.result_id;
		async_file_index.erase(it);
		lock.relock(NULL);

		IndexThread::removeResult(result_id);

		Server->Log("Async index " + async_id + " timeout", LL_DEBUG);

		tcpstack.Send(pipe, "error - Async indexing process timeout");
	}
	else
	{
		Server->Log("Wait for async index " + async_id, LL_DEBUG);
		state = CCSTATE_START_FILEBACKUP_ASYNC;
		++it->second.refcount;
		it->second.last_update = Server->getTimeMS();
		curr_result_id = it->second.result_id;
		IndexThread::refResult(curr_result_id);
	}
}

void ClientConnector::CMD_START_SHADOWCOPY(const std::string &cmd)
{
	if(cmd[cmd.size()-1]=='"')
	{
		state=CCSTATE_SHADOWCOPY;
		std::string dir=cmd.substr(10, cmd.size()-11);

		std::string clientsubname;
		if (dir.find("/") != std::string::npos)
		{
			std::string str_params = getafter("/", dir);
			dir = getuntil("/", dir);

			str_map params;
			ParseParamStrHttp(str_params, &params);

			clientsubname = params["clientsubname"];
		}

		CWData data;
		data.addChar(IndexThread::IndexThreadAction_ReferenceShadowcopy);
		curr_result_id = IndexThread::getResultId();
		data.addUInt(curr_result_id);
		data.addString(dir);
		data.addString(server_token);
		data.addUChar(0);
		data.addUChar(1);
		data.addString(clientsubname);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
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

		std::string clientsubname;
		int issues = 0;
		if (dir.find("/") != std::string::npos)
		{
			std::string str_params = getafter("/", dir);
			dir = getuntil("/", dir);

			str_map params;
			ParseParamStrHttp(str_params, &params);

			clientsubname = params["clientsubname"];

			str_map::iterator it_issues = params.find("issues");
			if (it_issues != params.end())
			{
				issues = watoi(it_issues->second);
			}
		}

		CWData data;
		data.addChar(IndexThread::IndexThreadAction_ReleaseShadowcopy);
		curr_result_id = IndexThread::getResultId();
		data.addUInt(curr_result_id);
		data.addString(dir);
		data.addString(server_token);
		data.addUChar(0);
		data.addInt(-1);
		data.addString(clientsubname);
		data.addInt(issues);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
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
		if(new_interval!=0 && new_interval!=backup_interval)
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
	IQuery *q=db->Prepare("SELECT id, name, path, tgroup, optional, server_default FROM backupdirs WHERE symlinked=0");
	int timeoutms=300;
	db_results res=q->Read(&timeoutms);

	IQuery* q_get_virtual_client = db->Prepare("SELECT virtual_client FROM virtual_client_group_offsets WHERE group_offset=?");

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
			int tgroup = watoi(res[i]["tgroup"]);
			cdir.set("group", tgroup%c_group_size);
			cdir.set("server_default", watoi(res[i]["server_default"]));

			if (tgroup < 0) continue;
			
			if (tgroup >= c_group_size)
			{
				int offset = (tgroup / c_group_size)*c_group_size;
				q_get_virtual_client->Bind(offset);
				db_results res_vc = q_get_virtual_client->Read();
				q_get_virtual_client->Reset();

				if (!res_vc.empty())
				{
					cdir.set("virtual_client", res_vc[0]["virtual_client"]);
				}
			}

			int flags = watoi(res[i]["optional"]);
			
			std::vector<std::pair<int, std::string> > flag_mapping = getFlagStrMapping();
			

			std::string str_flags;
			for (size_t j = 0; j < flag_mapping.size(); ++j)
			{
				if (flags & flag_mapping[j].first)
				{
					if (!str_flags.empty()) str_flags += ",";
					str_flags += flag_mapping[j].second;
				}
			}

			cdir.set("flags", str_flags);

			dirs.add(cdir);
		}

		ret.set("dirs", dirs);

        tcpstack.Send(pipe, ret.stringify(false));
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
		IScopedLock process_lock(process_mutex);

		SRunningProcess* proc = getRunningFileBackupProcess(std::string(), 0);

		if (proc != NULL)
		{
			removeRunningProcess(proc->id, true);
		}

		lasttime=Server->getTimeMS();
		status_updated = true;
	}
			
	IndexThread::execute_postbackup_hook("postfilebackup", 0, std::string());

	exit_backup_immediate(0);
}

void ClientConnector::CMD_DID_BACKUP2(const std::string &cmd)
{
	std::string params_str = cmd.substr(12);
	str_map params;
	ParseParamStrHttp(params_str, &params);

	updateLastBackup();
	tcpstack.Send(pipe, "OK");

	std::string server_token = params["server_token"];
	if (server_token.empty())
	{
		return;
	}

	{
		IScopedLock lock(backup_mutex);
		IScopedLock process_lock(process_mutex);

		SRunningProcess* proc = getRunningFileBackupProcess(server_token, watoi64(params["status_id"]));

		if (proc != NULL)
		{
			removeRunningProcess(proc->id, true);
		}

		lasttime = Server->getTimeMS();
		status_updated = true;
	}

	IndexThread::execute_postbackup_hook("postfilebackup", atoi(params["group"].c_str()), params["clientsubname"]);

	exit_backup_immediate(0);
}

void ClientConnector::CMD_BACKUP_FAILED(const std::string & cmd)
{
	std::string params_str = cmd.substr(14);
	str_map params;
	ParseParamStrHttp(params_str, &params);

	tcpstack.Send(pipe, "OK");

	std::string server_token = params["server_token"];
	if (server_token.empty())
	{
		return;
	}

	{
		IScopedLock lock(backup_mutex);
		IScopedLock process_lock(process_mutex);

		SRunningProcess* proc = getRunningFileBackupProcess(server_token, watoi64(params["status_id"]));

		if (proc != NULL)
		{
			removeRunningProcess(proc->id, false);
		}

		lasttime = Server->getTimeMS();
		status_updated = true;
	}

	IndexThread::execute_postbackup_hook("postfilebackup_failed", atoi(params["group"].c_str()), params["clientsubname"]);

	exit_backup_immediate(1);
}

int64 ClientConnector::getLastBackupTime()
{
	IDatabase *db=Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery *q=db->Prepare("SELECT strftime('%s',last_backup) AS last_backup FROM status", false);
	if (q == NULL)
		return 0;

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
	db->destroyQuery(q);

	if(res.size()>0)
	{
		return watoi64(res[0]["last_backup"]);
	}
	else
	{
		return 0;
	}
}

std::string ClientConnector::getCurrRunningJob(bool reset_done, int& pcdone)
{
	IScopedLock lock_process(process_mutex);

	SRunningProcess* proc = getActiveProcess(x_pingtimeout);

	if(proc==NULL )
	{
		lock_process.relock(NULL);
		return getHasNoRecentBackup();
	}
	else
	{
		pcdone = proc->pcdone;
		return actionToStr(proc->action);
	}
}

SChannel * ClientConnector::getCurrChannel()
{
	for (size_t i = 0; i < channel_pipes.size(); ++i)
	{
		if (channel_pipes[i].pipe == pipe)
		{
			return &channel_pipes[i];
		}
	}
	return NULL;
}

void ClientConnector::CMD_STATUS(const std::string &cmd)
{
	state=CCSTATE_STATUS;

	if (cmd == "FSTATUS")
	{
		sendStatus();
		state = CCSTATE_NORMAL;
	}
	else
	{
		lasttime = Server->getTimeMS();
	}
}

void ClientConnector::CMD_STATUS_DETAIL(const std::string &cmd)
{
	IScopedLock lock(backup_mutex);

	JSON::Object ret;

	ret.set("last_backup_time", getLastBackupTime());

	JSON::Array j_running_processes;

	for (size_t i = 0; i < running_processes.size(); ++i)
	{
		if (Server->getTimeMS() - running_processes[i].last_pingtime > x_pingtimeout)
		{
			continue;
		}

		JSON::Object obj;
		obj.set("percent_done", running_processes[i].pcdone);
		obj.set("action", actionToStr(running_processes[i].action));
		obj.set("eta_ms", running_processes[i].eta_ms);

		if (!running_processes[i].details.empty())
		{
			obj.set("details", running_processes[i].details);
		}

		if (running_processes[i].detail_pc != -1)
		{
			obj.set("detail_pc", running_processes[i].detail_pc);
		}

		if (running_processes[i].total_bytes >= 0)
		{
			obj.set("total_bytes", running_processes[i].total_bytes);
			obj.set("done_bytes", running_processes[i].done_bytes);
		}

		obj.set("process_id", running_processes[i].id);
		obj.set("server_status_id", running_processes[i].server_id);

		obj.set("speed_bpms", running_processes[i].speed_bpms);

		j_running_processes.add(obj);
	}

	ret.set("running_processes", j_running_processes);

	JSON::Array j_finished_processes;
	for (size_t i = 0; i < finished_processes.size(); ++i)
	{
		JSON::Object obj;
		obj.set("process_id", finished_processes[i].id);
		obj.set("success", finished_processes[i].success);
		j_finished_processes.add(obj);
	}

	ret.set("finished_processes", j_finished_processes);

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

	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	ret.set("capability_bits", getCapabilities(db));

    tcpstack.Send(pipe, ret.stringify(false));

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

	idle_timeout = 120000;

	IScopedLock lock(backup_mutex);
	IScopedLock process_lock(process_mutex);

	lasttime=Server->getTimeMS();
	std::string pcdone_new = getbetween("-", "-", cmd);

	last_token_times[server_token] = Server->getTimeSeconds();

	SRunningProcess* proc = getRunningFileBackupProcess(server_token, 0);

	if (proc == NULL)
	{
		return;
	}

	int pcdone_old = proc->pcdone;

	if (pcdone_new.empty())
		proc->pcdone = -1;
	else
		proc->pcdone = atoi(pcdone_new.c_str());

	if (pcdone_old != proc->pcdone)
	{
		status_updated = true;
	}

	proc->eta_ms = 0;
	proc->last_pingtime = Server->getTimeMS();	

#ifdef _WIN32
	SetThreadExecutionState(ES_SYSTEM_REQUIRED);
#endif
}

void ClientConnector::CMD_PING_RUNNING2(const std::string &cmd)
{
	std::string params_str = cmd.substr(14);
	str_map params;
	ParseParamStrHttp(params_str, &params);
	str_map::iterator it_paused_fb = params.find("paused_fb");
	if (it_paused_fb != params.end()
		&& it_paused_fb->second == "1"
		&& IdleCheckerThread::getPause())
	{
		tcpstack.Send(pipe, "PAUSED");
	}
	else
	{
		tcpstack.Send(pipe, "OK");
	}

	idle_timeout = 120000;

	IScopedLock lock(backup_mutex);
	IScopedLock process_lock(process_mutex);

	last_token_times[server_token] = Server->getTimeSeconds();
	lasttime=Server->getTimeMS();
	
	SRunningProcess* proc = getRunningBackupProcess(server_token, watoi64(params["status_id"]));

	if (proc == NULL)
	{
		return;
	}

	std::string pcdone_new=params["pc_done"];

	int pcdone_old = proc->pcdone;

	if(pcdone_new.empty())
		proc->pcdone =-1;
	else
		proc->pcdone =watoi(pcdone_new);

	if(pcdone_old!= proc->pcdone)
	{
		status_updated = true;
	}

	proc->eta_ms = watoi64(params["eta_ms"]);
	proc->last_pingtime = Server->getTimeMS();

	proc->speed_bpms = atof(params["speed_bpms"].c_str());
	proc->total_bytes = watoi64(params["total_bytes"]);
	proc->done_bytes = watoi64(params["done_bytes"]);

#ifdef _WIN32
	SetThreadExecutionState(ES_SYSTEM_REQUIRED);
#endif
}

void ClientConnector::CMD_CHANNEL(const std::string &cmd, IScopedLock *g_lock, const std::string& identity)
{
	bool img_download_running = false;
	{
		IScopedLock lock(process_mutex);

		SRunningProcess* proc = getRunningProcess(RUNNING_RESTORE_IMAGE, std::string());

		img_download_running = proc != NULL;
	}

	if(!img_download_running)
	{
		if (!FileExists("urbackup/access_keys.properties"))
		{
			tcpstack.Send(pipe, "UPDATE ACCESS KEY");
		}

#ifdef _WIN32
		bool metered;
		if (AllCurrentConnectionsMetered(metered))
		{
			IScopedLock lock(ident_mutex);
			if (last_metered != metered)
			{
				last_metered = metered;
				lock.relock(NULL);
				tcpstack.Send(pipe, std::string("METERED metered=") + (metered ? "1" : "0"));
			}
		}
#endif

		std::string token;

		std::string s_params = cmd.substr(9);
		str_map params;
		ParseParamStrHttp(s_params, &params);
		int capa = watoi(params["capa"]);
		token = params["token"];

		if (params["startup"] == "1")
		{
			tcpstack.Send(pipe, "STARTUP timestamp=" + convert(startup_timestamp));
		}

		g_lock->relock(backup_mutex);

		channel_pipes.push_back(SChannel(pipe, internet_conn, endpoint_name, token,
			&make_fileserv, identity, capa, watoi(params["restore_version"]), params["virtual_client"]));
		is_channel=true;
		state=CCSTATE_CHANNEL;
		last_channel_ping=Server->getTimeMS();
		lasttime=Server->getTimeMS();
		Server->Log("New channel: Number of Channels: "+convert((int)channel_pipes.size()), LL_DEBUG);
	}
}

void ClientConnector::CMD_CHANNEL_PONG(const std::string &cmd, const std::string& endpoint_name)
{
	lasttime=Server->getTimeMS();
	IScopedLock lock(backup_mutex);
	SChannel* chan = getCurrChannel();
	if (chan != NULL && chan->state == SChannel::EChannelState_Pinging)
	{
		chan->state = SChannel::EChannelState_Idle;
	}
	refreshSessionFromChannel(endpoint_name);
}

void ClientConnector::CMD_CHANNEL_PING(const std::string &cmd, const std::string& endpoint_name)
{
	lasttime=Server->getTimeMS();
	if(tcpstack.Send(pipe, "PONG")==0)
	{
		do_quit=true;
	}

	refreshSessionFromChannel(endpoint_name);
}

void ClientConnector::CMD_TOCHANNEL_START_INCR_FILEBACKUP(const std::string &cmd, str_map &params)
{
	tochannelSendStartbackup(RUNNING_INCR_FILE, params["virtual_client"]);
}

void ClientConnector::CMD_TOCHANNEL_START_FULL_FILEBACKUP(const std::string &cmd, str_map &params)
{
	tochannelSendStartbackup(RUNNING_FULL_FILE, params["virtual_client"]);
}

void ClientConnector::CMD_TOCHANNEL_START_FULL_IMAGEBACKUP(const std::string &cmd, str_map &params)
{
	tochannelSendStartbackup(RUNNING_FULL_IMAGE, params["virtual_client"]);
}

void ClientConnector::CMD_TOCHANNEL_START_INCR_IMAGEBACKUP(const std::string &cmd, str_map &params)
{
	tochannelSendStartbackup(RUNNING_INCR_IMAGE, params["virtual_client"]);
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
		CWData data;
		data.addChar(IndexThread::IndexThreadAction_UpdateCbt);
		IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

		tcpstack.Send(pipe, "NOSERVER");
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
	IScopedLock lock(backup_mutex);
	status_updated = true;
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
		image_inf.orig_image_letter = image_inf.image_letter;
		image_inf.server_status_id = watoi(params["status_id"]);
		image_inf.shadowdrive=(params["shadowdrive"]);
		if(params.find("start")!=params.end())
		{
			image_inf.startpos=watoi64(params["start"]);
		}
		else
		{
			image_inf.startpos=-1;
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

		image_inf.no_shadowcopy=false;

		image_inf.clientsubname = params["clientsubname"];

		if(image_inf.image_letter=="SYSVOL"
			|| image_inf.image_letter=="ESP")
		{
			std::string mpath;
			std::string sysvol;
			if(image_inf.image_letter=="SYSVOL")
			{
				sysvol=getSysVolumeCached(mpath);
			}
			else
			{
				sysvol=getEspVolumeCached(mpath);
			}
			
			if(!mpath.empty())
			{
				image_inf.image_letter=mpath;
			}
			else if(!sysvol.empty())
			{
				image_inf.image_letter=sysvol;
				image_inf.no_shadowcopy=true;
			}
			else
			{
				ImageErr("Not found");
				return;
			}
		}
#ifndef _WIN32
		else if (image_inf.image_letter == "C"
			|| image_inf.image_letter == "C:")
		{
			image_inf.image_letter = getRootVol();
		}
#endif

		int running_jobs = 2;
		if (params.find("running_jobs") != params.end())
		{
			running_jobs = watoi(params["running_jobs"]);
		}

		if (image_inf.shadow_id != -1)
		{
			image_inf.shadowdrive.clear();
			CWData data;
			data.addChar(4);
			curr_result_id = IndexThread::getResultId();
			data.addUInt(curr_result_id);
			data.addInt(image_inf.shadow_id);
			IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
		}
		else if(!image_inf.no_shadowcopy)
		{
			CWData data;
			data.addChar(IndexThread::IndexThreadAction_CreateShadowcopy);
			curr_result_id = IndexThread::getResultId();
			data.addUInt(curr_result_id);
			data.addString(image_inf.image_letter);
			data.addString(server_token);
			data.addUChar(1); //full image backup
			data.addUChar(0); //filesrv
			data.addString(image_inf.clientsubname);
			data.addInt(running_jobs);
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
			image_inf.server_status_id = watoi(params["status_id"]);
			if(params.find("start")!=params.end())
			{
				image_inf.startpos=watoi64(params["start"]);
			}
			else
			{
				image_inf.startpos=-1;
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
			
			image_inf.no_shadowcopy=false;
			image_inf.clientsubname = params["clientsubname"];

#ifndef _WIN32
			if (image_inf.image_letter == "C"
				|| image_inf.image_letter == "C:")
			{
				image_inf.image_letter = getRootVol();
			}
#endif

			str_map::iterator f_cbitmapsize = params.find("cbitmapsize");
			if (f_cbitmapsize != params.end())
			{
				bitmapleft = watoi(f_cbitmapsize->second);

				bitmapfile = Server->openTemporaryFile();
				if (bitmapfile == NULL)
				{
					Server->Log("Error creating temporary bitmap file in CMD_INCR_IMAGE", LL_ERROR);
					do_quit = true;
					return;
				}
			}
			else
			{
				bitmapfile = NULL;
			}

			int running_jobs = 2;
			if (params.find("running_jobs") != params.end())
			{
				running_jobs = watoi(params["running_jobs"]);
			}

			if (image_inf.shadow_id != -1)
			{
				image_inf.shadowdrive.clear();
				CWData data;
				data.addChar(4);
				curr_result_id = IndexThread::getResultId();
				data.addUInt(curr_result_id);
				data.addInt(image_inf.shadow_id);
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			}
			else if(!image_inf.no_shadowcopy)
			{
				CWData data;
				data.addChar(IndexThread::IndexThreadAction_CreateShadowcopy);
				curr_result_id = IndexThread::getResultId();
				data.addUInt(curr_result_id);
				data.addString(image_inf.image_letter);
				data.addString(server_token);
				data.addUChar(2); //incr image backup
				data.addUChar(0); //file serv?
				data.addString(image_inf.clientsubname);
				data.addInt(running_jobs);
				IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());
			}

			hashdatafile=Server->openTemporaryFile();
			if(hashdatafile==NULL)
			{
				Server->Log("Error creating temporary file in CMD_INCR_IMAGE", LL_ERROR);
				do_quit=true;
				return;
			}

			state = CCSTATE_IMAGE_HASHDATA;

			size_t bufpos = 0;
			IFile* datafile = hashdatafile;
			_u32* dataleft = &hashdataleft;
			while(bufpos<tcpstack.getBuffersize())
			{
				_u32 towrite = (std::min)(static_cast<_u32>(tcpstack.getBuffersize() - bufpos), *dataleft);
				if(datafile->Write(tcpstack.getBuffer()+ bufpos, towrite)!= towrite)
				{
					Server->Log("Error writing to data temporary file in CMD_INCR_IMAGE", LL_ERROR);
					do_quit=true;
					return;
				}
				
				bufpos += towrite;
				*dataleft -= towrite;

				tcpstack.removeFront(towrite);

				if(*dataleft == 0)
				{
					if (bitmapfile == NULL
						|| datafile==bitmapfile)
					{
						hashdataok = true;
						state = CCSTATE_IMAGE;
						break;
					}
					else
					{
						datafile = bitmapfile;
						dataleft = &bitmapleft;
						state = CCSTATE_IMAGE_BITMAP;
					}
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

	if(dl=="SYSVOL")
	{
		std::string mpath;
		dl=getSysVolumeCached(mpath);
	}
	else if(dl=="ESP")
	{
		std::string mpath;
		dl=getEspVolumeCached(mpath);
	}
#ifndef _WIN32
	else if (dl == "C" || dl == "C:")
	{
		dl = getRootVol();
	}
	else if(params.find("disk_path")!=params.end())
	{
		dl=params["disk_path"];
	}
#endif

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
		tcpstack.Send(pipe, "1");
	}
	else
	{

		std::string utf8_tokens = params["tokens"];
		std::string filebackups;
		std::string accessparams;
		std::string virtual_client = params["virtual_client"];

		if(!multipleChannelServers())
		{
			accessparams+=" with_id_offset=false";
		}

		bool has_token_params=false;

		for(size_t i=0;i<channel_pipes.size();++i)
		{
			if (channel_pipes[i].virtual_client != virtual_client)
			{
				continue;
			}

			for(size_t j=0;j<2;++j)
			{
				if(channel_pipes[i].last_tokens!=utf8_tokens)
				{
					if(!has_token_params)
					{
						std::string token_params = getAccessTokensParams(params["tokens"], true, virtual_client);

						if(token_params.empty())
						{
							tcpstack.Send(pipe, "2");
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
				std::string nc=receivePacket(channel_pipes[i],
					10*60*1000);
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
            tcpstack.Send(pipe, "3");
        }
        else
        {
            tcpstack.Send(pipe, "0"+filebackups);
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
		tcpstack.Send(pipe, "1");
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

		if(!multipleChannelServers())
		{
			accessparams+="&with_id_offset=false";
		}

        std::string filelist;

		if(!accessparams.empty())
		{
			accessparams[0]=' ';
		}

		std::string utf8_tokens = params["tokens"];
		std::string virtual_client = params["virtual_client"];
		bool has_token_params=false;
		bool break_outer=false;
		for(size_t i=0;i<channel_pipes.size();++i)
		{
			if (channel_pipes[i].virtual_client != virtual_client)
			{
				continue;
			}

			for(size_t j=0;j<2;++j)
			{
				if(channel_pipes[i].last_tokens!=utf8_tokens)
				{
					if(!has_token_params)
					{
						std::string token_params = getAccessTokensParams(params["tokens"], true, virtual_client);

						if(token_params.empty())
						{
							tcpstack.Send(pipe, "2");
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
            tcpstack.Send(pipe, "0"+filelist);
        }
        else
        {
            tcpstack.Send(pipe, "3");
        }
	}
}

void ClientConnector::CMD_DOWNLOAD_FILES_TOKENS(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	IScopedLock lock(backup_mutex);
	waitForPings(&lock);
	if(channel_pipes.size()==0)
	{
		tcpstack.Send(pipe, "1");
		return;
	}

	std::string accessparams;

	str_map::iterator it_path = params.find("path");

	if(it_path==params.end())
	{
		tcpstack.Send(pipe, "2");
		return;
	}
	
	accessparams+="&path="+EscapeParamString((it_path->second));
	

	str_map::iterator it_backupid = params.find("backupid");

	if(it_backupid==params.end())
	{
		tcpstack.Send(pipe, "3");
		return;
	}
	
	accessparams+="&backupid="+EscapeParamString(it_backupid->second);
	
	if(!multipleChannelServers())
	{
		accessparams+="&with_id_offset=false";
	}

	std::string restore_token_binary;
	restore_token_binary.resize(16);
	Server->secureRandomFill(&restore_token_binary[0], restore_token_binary.size());

	restore_token.restore_token = bytesToHex(restore_token_binary);
	restore_token.restore_token_time = Server->getTimeMS();

	{
		IScopedLock process_lock(process_mutex);
		restore_token.process_id = ++curr_backup_running_id;
	}

	accessparams+="&restore_token="+ restore_token.restore_token+"&process_id="+convert(restore_token.process_id);

	str_map::iterator it_clean_other = params.find("clean_other");
	if (it_clean_other != params.end())
	{
		accessparams += "&clean_other=" + EscapeParamString(it_clean_other->second);
	}

	str_map::iterator it_ignore_other_fs = params.find("ignore_other_fs");
	if (it_clean_other != params.end())
	{
		accessparams += "&ignore_other_fs=" + EscapeParamString(it_ignore_other_fs->second);
	}

	str_map::iterator it_follow_symlinks = params.find("follow_symlinks");
	if (it_follow_symlinks != params.end())
	{
		accessparams += "&follow_symlinks=" + EscapeParamString(it_follow_symlinks->second);
	}

	str_map::iterator it_restore_flags = params.find("restore_flags");
	if (it_restore_flags != params.end())
	{
		accessparams += "&restore_flags=" + EscapeParamString(it_restore_flags->second);
	}

	for (size_t i = 0; params.find("map_path_source" + convert(i)) != params.end(); ++i)
	{
		accessparams += "&map_path_source" + convert(i) + "=" + EscapeParamString(params["map_path_source" + convert(i)]);
		accessparams += "&map_path_target" + convert(i) + "=" + EscapeParamString(params["map_path_target" + convert(i)]);
	}

	std::string filelist;

	accessparams[0]=' ';

	std::string utf8_tokens = params["tokens"];
	std::string virtual_client = params["virtual_client"];
	bool has_token_params=false;
	std::string last_err;
	for(size_t i=0;i<channel_pipes.size();++i)
	{
		if (channel_pipes[i].virtual_client != virtual_client)
		{
			continue;
		}

		for(size_t j=0;j<2;++j)
		{
			if(channel_pipes[i].last_tokens!=utf8_tokens)
			{
				if(!has_token_params)
				{
					std::string token_params = getAccessTokensParams(params["tokens"], true, virtual_client);

					if(token_params.empty())
					{
						tcpstack.Send(pipe, "error encrypting access tokens");
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

			last_err=receivePacket(channel_pipes[i]);
			if(!last_err.empty() && last_err!="err")
			{
				channel_pipes[i].last_tokens = utf8_tokens;

				tcpstack.Send(pipe, "0" + last_err);
				return;
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

	tcpstack.Send(pipe, last_err);
}

void ClientConnector::CMD_RESTORE_DOWNLOAD_IMAGE(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	Server->Log("Downloading image...", LL_DEBUG);
	IScopedLock lock(backup_mutex);
	waitForPings(&lock);
	Server->Log("In mutex...",LL_DEBUG);
	downloadImage(params, lock);
	Server->Log("Download done -2", LL_DEBUG);
	do_quit=true;
}

void ClientConnector::CMD_RESTORE_DOWNLOAD_FILES(const std::string &cmd, str_map &params)
{
	lasttime=Server->getTimeMS();
	Server->Log("Downloading files...", LL_DEBUG);
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

		std::string with_id_offset = "";
		if (multipleChannelServers())
		{
			with_id_offset += "&with_id_offset=true";
		}

		CTCPStack recv_stack(channel_pipes[i].internet_connection);
		recv_stack.Send(c, "DOWNLOAD FILES backupid="+params["backupid"]+"&time="+params["time"]+ with_id_offset);

		Server->Log("Start downloading files from channel "+convert((int)i), LL_DEBUG);

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

	bool img_download_running = false;
	{
		IScopedLock lock(process_mutex);
		SRunningProcess* proc = getRunningProcess(RUNNING_RESTORE_IMAGE, std::string());
		img_download_running = proc != NULL;
	}

	if(!img_download_running)
	{
		pipe->Write("100");
		do_quit=true;
	}
	else
	{
		while(true)
		{
			{
				int progress=0;
				{
					IScopedLock lock(process_mutex);
					SRunningProcess* proc = getRunningProcess(RUNNING_RESTORE_IMAGE, std::string());
					if (proc != NULL)
					{
						progress = proc->pcdone;
					}
					else
					{
						break;
					}
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
	std::string server_version = cmd.substr(8);

#ifdef _WIN32
#define VERSION_FILE_PREFIX ""
#else
#define VERSION_FILE_PREFIX "urbackup/"

	if (atoi(server_version.c_str()) < 125)
	{
		tcpstack.Send(pipe, "noop");
		return;
	}
#endif

#if defined(_WIN32) || defined(URB_WITH_CLIENTUPDATE)
	std::string version_1=getFile(VERSION_FILE_PREFIX "version.txt");
	std::string version_2=getFile(VERSION_FILE_PREFIX "curr_version.txt");
	if(version_1.empty() ) version_1="0";
	if(version_2.empty() ) version_2="0";

	if(versionNeedsUpdate(version_1, server_version)
		&& versionNeedsUpdate(version_2, server_version) )
	{
		tcpstack.Send(pipe, "update");
	}
	else
	{
		tcpstack.Send(pipe, "noop");
	}
#else
	tcpstack.Send(pipe, "noop");
	return;
#endif

#undef VERSION_FILE_PREFIX
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
	
	str_map params;
	ParseParamStrHttp(cmd.substr(14), &params);

	hashdataleft=watoi(params["size"]);
	silent_update=(params["silent_update"]=="true");

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

		tcpstack.reset();

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
	str_map params;
	if (cmd.size() > 5)
	{
		ParseParamStrHttp(cmd.substr(5), &params);
	}

	std::string clientsubname;
	str_map::iterator it_clientsubname = params.find("clientsubname");
	if (it_clientsubname != params.end())
		clientsubname = it_clientsubname->second;

	std::string client_version_str=std::string(c_client_version);

	std::string restore=Server->getServerParameter("allow_restore");

	if(restore.empty() || restore=="default")
	{
		restore="client-confirms";
	}

	std::string clientuid;
	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);
	IQuery* q_get_uid = db->Prepare("SELECT tvalue FROM misc WHERE tkey=?", false);
	q_get_uid->Bind("clientuid_" + clientsubname);
	db_results res = q_get_uid->Read();
	if (res.empty())
	{
		IQuery* q_ins_uid = db->Prepare("INSERT INTO misc (tkey, tvalue) VALUES (?,?)", false);
		q_ins_uid->Bind("clientuid_" + clientsubname);
		q_ins_uid->Bind(Server->secureRandomString(16));
		q_ins_uid->Write();
		db->destroyQuery(q_ins_uid);
		res = q_get_uid->Read();
	}
	db->destroyQuery(q_get_uid);
	
	if (!res.empty())
	{
		clientuid = res[0]["tvalue"];
	}

	std::string imm_backup = Server->getServerParameter("backup_immediate");
	if (!imm_backup.empty())
	{
		imm_backup = "&BACKUP=" + EscapeParamString(imm_backup);
	}

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

	std::string send_prev_cbitmap;
	if (os_get_file_type("urbctctl.exe")!=0)
	{
		send_prev_cbitmap = "&REQ_PREV_CBITMAP=1";
	}

	std::string conn_metered = "";
	bool metered;
	if (AllCurrentConnectionsMetered(metered))
	{
		conn_metered = std::string("&METERED=") + (metered ? "1" : "0");
		IScopedLock lock(ident_mutex);
		last_metered = metered;
	}

	tcpstack.Send(pipe, "FILE=2&FILE2=1&IMAGE=1&UPDATE=1&MBR=1&FILESRV=3&SET_SETTINGS=1&IMAGE_VER=1&CLIENTUPDATE=2&ASYNC_INDEX=1"
		"&CLIENT_VERSION_STR="+EscapeParamString((client_version_str))+"&OS_VERSION_STR="+EscapeParamString(os_version_str)+
		"&ALL_VOLUMES="+EscapeParamString(win_volumes)+"&ETA=1&CDP=0&ALL_NONUSB_VOLUMES="+EscapeParamString(win_nonusb_volumes)+"&EFI=1"
		"&FILE_META=1&SELECT_SHA=1&PHASH=1&RESTORE="+restore+"&RESTORE_VER=1&CLIENT_BITMAP=1&CMD=2&SYMBIT=1&WTOKENS=1&FILESRVTUNNEL=1&OS_SIMPLE=windows"
		"&clientuid="+EscapeParamString(clientuid)+conn_metered+ send_prev_cbitmap + imm_backup);
#else

#ifdef __APPLE__
	std::string os_simple = "osx";
#elif __linux__
	std::string os_simple = "linux";
#else
	std::string os_simple = "unknown";
#endif

	std::string image_args = "&IMAGE=0";
	if(!trim(IndexThread::get_snapshot_script_location("create_volume_snapshot", clientsubname)).empty())
	{
		image_args = "&IMAGE=1&REQ_PREV_CBITMAP=1";
	}


	std::string os_version_str=get_lin_os_version();
	tcpstack.Send(pipe, "FILE=2&FILE2=1&FILESRV=3&SET_SETTINGS=1&IMAGE_VER=1&CLIENTUPDATE=2&ASYNC_INDEX=1"
		"&CLIENT_VERSION_STR="+EscapeParamString((client_version_str))+"&OS_VERSION_STR="+EscapeParamString(os_version_str)
		+"&ETA=1&CPD=0&EFI=1&FILE_META=1&SELECT_SHA=1&PHASH=1&RESTORE="+restore+"&RESTORE_VER=1&CLIENT_BITMAP=1&CMD=2&SYMBIT=1&WTOKENS=1&FILESRVTUNNEL=1&OS_SIMPLE="+os_simple
		+"&clientuid=" + EscapeParamString(clientuid) + imm_backup + image_args);
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

void ClientConnector::CMD_RESET_KEEP(str_map &params)
{
	std::string virtual_client = params["virtual_client"];
	std::string folder_name = params["folder_name"];
	int tgroup = watoi(params["tgroup"]);

	IDatabase *db = Server->getDatabase(Server->getThreadID(), URBACKUPDB_CLIENT);

	int group_offset = 0;
	if (!virtual_client.empty())
	{
		IQuery *q = db->Prepare("SELECT group_offset FROM virtual_client_group_offsets WHERE virtual_client=?", false);
		q->Bind(virtual_client);
		db_results res = q->Read();
		db->destroyQuery(q);
		
		if (res.empty())
		{
			tcpstack.Send(pipe, "err_virtual_client_not_found");
			return;
		}

		group_offset = watoi(res[0]["group_offset"]);
	}

	IQuery* q = db->Prepare(std::string("UPDATE backupdirs SET reset_keep=1 WHERE tgroup=?")
		+ (folder_name.empty() ? "" : " AND name=?"), false);
	q->Bind(tgroup + group_offset);
	if (!folder_name.empty())
	{
		q->Bind(folder_name);
	}
	q->Write();
	q->Reset();

	if (db->getLastChanges() > 0)
	{
		tcpstack.Send(pipe, "OK");
	}
	else
	{
		tcpstack.Send(pipe, "err_backup_folder_not_found");
	}

	db->destroyQuery(q);
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
	unsigned int result_id = IndexThread::getResultId();
	data.addUInt(result_id);
	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	std::string ret;
	IndexThread::getResult(result_id, 8000, ret);
	tcpstack.Send(pipe, ret);
	IndexThread::removeResult(result_id);
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

	std::string ret = getAccessTokensParams(tokens, true, params["virtual_client"]);

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
		tcpstack.Send(pipe, convert(exit_code)+" "+convert(Server->getTimeMS())+" "+stderr_out);
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

	int64 restore_process_id = 0;

	bool has_restore_token = false;
	if (!restore_token.restore_token.empty()
		&& params["restore_token"] == restore_token.restore_token
		&& Server->getTimeMS() - restore_token.restore_token_time<60*60*1000)
	{
		restore_token.restore_token = "";
		has_restore_token = true;
		restore_process_id = restore_token.process_id;
		restore_token.process_id = 0;
	}

	std::string restore=Server->getServerParameter("allow_restore");

	if(restore=="default")
	{
		restore="client-confirms";
	}

	if(restore!="client-confirms" && restore!="server-confirms"
		&& !has_restore_token)
	{
		tcpstack.Send(pipe, "disabled");
		return;
	}

	bool client_token_encrypted = true;
	std::string client_token = (params["client_token"]);
	if (client_token.empty()
		&& params.find("client_token_d") != params.end())
	{
		client_token = params["client_token_d"];
		client_token_encrypted = false;
	}
	std::string server_token=params["server_token"];
	int64 restore_id=watoi64(params["id"]);
	int64 status_id=watoi64(params["status_id"]);
	int64 log_id=watoi64(params["log_id"]);
	std::string restore_path = params["restore_path"];
	bool single_file = params["single_file"]=="1";
	bool clean_other = params["clean_other"] == "1";
	bool ignore_other_fs = params["ignore_other_fs"] != "0";
	int tgroup = watoi(params["tgroup"]);
	std::string clientsubname = params["clientsubname"];
	int64 restore_flags = 0;
	str_map::iterator restore_flags_it = params.find("restore_flags");
	if (restore_flags_it != params.end())
	{
		restore_flags = watoi64(restore_flags_it->second);
	}

	if(restore_process_id==0)
	{
		IScopedLock lock(process_mutex);
		restore_process_id = ++curr_backup_running_id;
	}

	if (crypto_fak != NULL
		&& client_token_encrypted)
	{
		std::auto_ptr<ISettingsReader> access_keys(
			Server->createFileSettingsReader("urbackup/access_keys.properties"));

		if (access_keys.get() != NULL)
		{
			std::string access_key;
			if (access_keys->getValue("key." + server_token, &access_key))
			{
				client_token = crypto_fak->decryptAuthenticatedAES(base64_decode_dash(client_token), access_key, 1);

				if (client_token.empty()
					&& access_keys->getValue("last.key." + server_token, &access_key))
				{
					client_token = crypto_fak->decryptAuthenticatedAES(base64_decode_dash(client_token), access_key, 1);
				}

				if (client_token.empty())
				{
					Server->Log("Error decrypting server access token. Access key might be wrong.", LL_ERROR);
				}
			}
			else
			{
				Server->Log("key."+ server_token+" not found in urbackup/access_keys.properties", LL_ERROR);
			}
		}
		else
		{
			Server->Log("Error opening urbackup/access_keys.properties", LL_ERROR);
		}
	}
	else if(client_token_encrypted)
	{
		Server->Log("Error decrypting server access token. Crypto_fak not loaded.", LL_ERROR);
	}

	RestoreFiles* local_restore_files = new RestoreFiles(restore_process_id, restore_id, status_id, log_id,
		client_token, server_token, restore_path, single_file, clean_other, ignore_other_fs, restore_flags,
		tgroup, clientsubname);

	if(restore == "client-confirms" && !has_restore_token)
	{
		IScopedLock lock(backup_mutex);
		restore_ok_status = RestoreOk_Wait;
		++ask_restore_ok;
		status_updated = true;

		if(restore_files!=NULL)
		{
			delete restore_files;
		}

		restore_files = local_restore_files;
	}
	else
	{
		Server->createThread(local_restore_files, "file restore", IServer::CreateThreadFlags_LargeStackSize);
	}

	tcpstack.Send(pipe, "ok");
}

void ClientConnector::CMD_RESTORE_OK( str_map &params )
{
	IScopedLock lock(backup_mutex);
	JSON::Object ret;
	if(params["ok"]=="true")
	{
		restore_ok_status = RestoreOk_Ok;

		ret.set("accepted", true);

		if(restore_files!=NULL)
		{
			ret.set("process_id", restore_files->get_local_process_id());

			Server->createThread(restore_files, "file restore", IServer::CreateThreadFlags_LargeStackSize);
			restore_files=NULL;
		}
	}
	else
	{
		restore_ok_status = RestoreOk_Declined;

		if (restore_files != NULL)
		{
			restore_files->set_restore_declined(true);
			Server->createThread(restore_files, "file restore", IServer::CreateThreadFlags_LargeStackSize);
			restore_files = NULL;
		}
	}

	delete restore_files;
	restore_files=NULL;

	ret.set("ok", true);
	tcpstack.Send(pipe, ret.stringify(false));
}

void ClientConnector::CMD_CLIENT_ACCESS_KEY(const std::string& cmd)
{
	str_map params;
	ParseParamStrHttp(cmd, &params);

	std::string key = params["key"];
	std::string token = params["token"];

	if (!key.empty()
		&& !token.empty()
		&& !FileExists("urbackup/access_keys.properties") )
	{
		std::string access_keys_data;
		access_keys_data += "key." + token + "=" +
			key + "\n";

		access_keys_data += "key_age." + token + "=" +
			convert(Server->getTimeSeconds()) + "\n";

		write_file_only_admin(access_keys_data, "urbackup/access_keys.properties.new");
		os_rename_file("urbackup/access_keys.properties.new",
			"urbackup/access_keys.properties");

		tcpstack.Send(pipe, "OK");
	}
	else
	{
		tcpstack.Send(pipe, "err: key or token empty");
	}
}

void ClientConnector::CMD_WRITE_TOKENS(const std::string& cmd)
{
	str_map params;
	ParseParamStrHttp(cmd, &params);

	std::string async_id;
	async_id.resize(16);
	Server->randomFill(&async_id[0], async_id.size());

	curr_result_id = IndexThread::getResultId();

	SAsyncFileList new_async_file_list = {
		Server->getTimeMS(),
		curr_result_id,
		0
	};

	CWData data;
	data.addChar(IndexThread::IndexThreadAction_WriteTokens);
	data.addUInt(curr_result_id);
	data.addString(server_token);

	IndexThread::getMsgPipe()->Write(data.getDataPtr(), data.getDataSize());

	std::string msg;
	if (IndexThread::getResult(curr_result_id, 3000, msg)
		&& !msg.empty())
	{
		if(msg=="done")
			tcpstack.Send(pipe, "OK");
		else
			tcpstack.Send(pipe, msg);
		IndexThread::removeResult(curr_result_id);
		return;
	}

	IndexThread::refResult(curr_result_id);

	IScopedLock lock(backup_mutex);

	async_file_index[async_id] = new_async_file_list;

	lock.relock(NULL);

	tcpstack.Send(pipe, "ASYNC-async_id=" + bytesToHex(async_id));
}

void ClientConnector::CMD_GET_CLIENTNAME(const std::string& cmd)
{
	std::string name = IndexThread::getFileSrv()->getServerName();
	tcpstack.Send(pipe, "name="+EscapeParamString(name));
}
	
