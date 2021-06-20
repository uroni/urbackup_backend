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

#include "InternetClient.h"

#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/ThreadPool.h"

#include "client.h"
#include "ClientService.h"

#include "../common/data.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/InternetServiceIDs.h"
#include "../urbackupcommon/InternetServicePipe2.h"
#include "../urbackupcommon/internet_pipe_capabilities.h"
#include "../urbackupcommon/CompressedPipe2.h"
#include "../urbackupcommon/CompressedPipeZstd.h"
#include "../urbackupcommon/WebSocketPipe.h"

#include "../stringtools.h"

#ifndef _WIN32
#include "../config.h"
#endif

#include <stdlib.h>
#include <memory.h>
#include <assert.h>

#include "../cryptoplugin/ICryptoFactory.h"

extern ICryptoFactory *crypto_fak;
const unsigned int pbkdf2_iterations=20000;

bool InternetClient::connected=false;
int64 InternetClient::last_lan_connection=0;
std::set<int> InternetClient::update_settings;
SServerSettings InternetClient::server_settings;
bool InternetClient::do_exit=false;
IMutex* InternetClient::g_mutex = nullptr;
std::map<int, std::string> InternetClient::status_msgs;
std::map<int, ICondition*> InternetClient::running_client_facets;

const unsigned int ic_lan_timeout=10*60*1000;
const unsigned int spare_connections=1;
const unsigned int ic_auth_timeout=10000;
const unsigned int ic_ping_timeout=6*60*1000;
const unsigned int ic_backup_running_ping_timeout=60*1000;
const unsigned int ic_restore_ping_timeout = 60 * 1000;
const int ic_sleep_after_auth_errs=2;

const char SERVICE_COMMANDS=0;
const char SERVICE_FILESRV=1;

namespace
{
	std::string formatServerForLog(const SServerConnectionSettings& ss)
	{
		if (next(ss.hostname, 0, "ws://") ||
			next(ss.hostname, 0, "wss://"))
		{
			return ss.hostname;
		}

		return "urbackup://"+ss.hostname+":"+convert(ss.port)
			+ (ss.proxy.empty() ? "" : (" via HTTP proxy " + ss.proxy));
	}
}

void InternetClient::init_mutex(void)
{
	g_mutex = Server->createMutex();
}

void InternetClient::destroy_mutex(void)
{
	Server->destroy(g_mutex);
}

std::string InternetClientThread::generateRandomBinaryAuthKey(void)
{
	std::string key;
	key.resize(32);
	Server->secureRandomFill((char*)key.data(), 32);
	return key;
}

void InternetClient::hasLANConnection(void)
{
	IScopedLock lock(g_mutex);
	last_lan_connection=Server->getTimeMS();
}

int64 InternetClient::timeSinceLastLanConnection()
{
	int64 ctime=Server->getTimeMS();
	IScopedLock lock(g_mutex);

	if(ctime>last_lan_connection)
	{
		return ctime-last_lan_connection;
	}
	else
	{
		return 0;
	}
}

bool InternetClient::isConnected(void)
{
	IScopedLock lock(g_mutex);
	return connected;
}

void InternetClient::setHasConnection(bool b)
{
	IScopedLock lock(g_mutex);
	connected=b;
}

void InternetClient::newConnection(void)
{
	IScopedLock lock(mutex);
	++n_connections;
}

void InternetClient::rmConnection(void)
{
	IScopedLock lock(mutex);
	--n_connections;
	wakeup_cond->notify_all();
}

void InternetClient::updateSettings(void)
{
	IScopedLock lock(g_mutex);
	for (auto& it : running_client_facets)
	{
		update_settings.insert(it.first);
		it.second->notify_one();
	}
}

void InternetClient::setHasAuthErr(void)
{
	IScopedLock lock(mutex);
	++auth_err;
}

void InternetClient::resetAuthErr(void)
{
	IScopedLock lock(mutex);
	auth_err=0;
}

void InternetClient::operator()(void)
{
	{
		IScopedLock lock(g_mutex);
		running_client_facets[facet_id] = wakeup_cond;
	}
	Server->waitForStartupComplete();

	setStatusMsg("wait_local");
	doUpdateSettings();

	if(Server->getServerParameter("internet_only_mode")!="true")
	{
		const int64 wait_time_ms=180000;

		bool has_server = !server_settings.servers.empty();

		if (has_server)
		{
			Server->Log("Internet only mode not enabled. Waiting for local server for " + FormatTime(wait_time_ms / 1000) + "...", LL_DEBUG);
		}
		int64 wait_starttime = Server->getTimeMS();
		while (Server->getTimeMS() - wait_starttime < wait_time_ms)
		{
			Server->wait(1000);
			{
				IScopedLock lock(g_mutex);
				if (update_settings.find(facet_id)!=update_settings.end())
				{
					update_settings.erase(facet_id);
					lock.relock(mutex);

					doUpdateSettings();

					if (!has_server
						&& !server_settings.servers.empty())
					{
						break;
					}
				}
			}	
		}
	}
	else
	{
		Server->wait(1000);
	}
	doUpdateSettings();
	while(!do_exit)
	{
		IScopedLock lock(g_mutex);
		if(update_settings.find(facet_id) != update_settings.end())
		{
			update_settings.erase(facet_id);
			lock.relock(mutex);
			doUpdateSettings();
		}

		if(server_settings.internet_connect_always || last_lan_connection==0 || Server->getTimeMS()-last_lan_connection>ic_lan_timeout)
		{
			if(!connected)
			{
				if(tryToConnect(&lock))
				{
					connected=true;
				}
				else
				{
					wakeup_cond->wait(&lock, ic_lan_timeout/2);
				}
			}
			else
			{
				if(n_connections<spare_connections)
				{
					Server->getThreadPool()->execute(new InternetClientThread(this, nullptr, server_settings, nullptr), "internet client");
					newConnection();
				}
				else
				{
					wakeup_cond->wait(&lock);
					if(auth_err>=ic_sleep_after_auth_errs)
					{
						lock.relock(nullptr);
						Server->wait(ic_lan_timeout/2);
					}
				}
			}
		}
		else
		{
			setStatusMsg("connected_local");
			wakeup_cond->wait(&lock, ic_lan_timeout);
		}
	}

	delete this;
}

void InternetClient::doUpdateSettings(void)
{
	server_settings.servers.clear();

	ISettingsReader *settings=Server->createFileSettingsReader(settings_fn);
	if(settings==nullptr)
	{
		Server->Log("Cannot open settings in InternetClient", LL_WARNING);
		return;
	}

	std::string internet_mode_enabled;
	if( !settings->getValue("internet_mode_enabled", &internet_mode_enabled) || internet_mode_enabled=="false" )
	{
		if( !settings->getValue("internet_mode_enabled_def", &internet_mode_enabled) || internet_mode_enabled=="false" )
		{
			Server->destroy(settings);
			if(Server->getServerParameter("internet_only_mode")=="true")
			{
				Server->Log("Internet mode not enabled. Please set \"internet_mode_enabled\" to \"true\".", LL_ERROR);
			}
			else
			{
				Server->Log("Internet mode not enabled", LL_DEBUG);
			}
			return;
		}
	}

	std::string server_name;
	std::string computername;
	std::string server_port="55415";
	std::string server_proxy;
	std::string authkey;
	if(!settings->getValue("internet_authkey", &authkey) && !settings->getValue("internet_authkey_def", &authkey))
	{
		Server->destroy(settings);
		if(Server->getServerParameter("internet_only_mode")=="true")
		{
			Server->Log("Internet authentication key not configured. Please configure \"internet_authkey\".", LL_ERROR);
			exit(2);
		}
		else
		{
			Server->Log("Internet authentication key not configured", LL_INFO);
		}
		return;
	}
	if( (!settings->getValue("computername", &computername)
		 && !settings->getValue("computername_def", &computername) ) 
		   || computername.empty())
	{
		computername=(IndexThread::getFileSrv()->getServerName());		
	}
	if (!settings->getValue("internet_server_proxy", &server_proxy))
		settings->getValue("internet_server_proxy_def", &server_proxy);
	if( (settings->getValue("internet_server", &server_name) || settings->getValue("internet_server_def", &server_name))
		&& !server_name.empty() )
	{
		if(!settings->getValue("internet_server_port", &server_port) )
			settings->getValue("internet_server_port_def", &server_port);

		std::vector<std::string> server_names;
		Tokenize(server_name, server_names, ";");

		std::vector<std::string> server_ports;
		Tokenize(server_port, server_ports, ";");

		std::vector<std::string> server_proxies;
		Tokenize(server_proxy, server_proxies, ";");

		for(size_t i=0;i<server_names.size();++i)
		{
			SServerConnectionSettings connection_settings;
			connection_settings.hostname = server_names[i];
			if(i<server_ports.size())
			{
				connection_settings.port = static_cast<unsigned short>(atoi(server_ports[i].c_str()));
			}
			else if(!server_ports.empty())
			{
				connection_settings.port = static_cast<unsigned short>(atoi(server_ports[server_ports.size() - 1].c_str()));
			}
			if (i < server_proxies.size())
				connection_settings.proxy = server_proxies[i];
			else if(!server_proxies.empty())
				connection_settings.proxy = server_proxies[server_proxies.size()-1];

			server_settings.servers.push_back(connection_settings);
		}
		server_settings.clientname=computername;
		server_settings.authkey=authkey;
	}
	else
	{
		if(Server->getServerParameter("internet_only_mode")=="true")
		{
			Server->Log("Internet server not configured. Please configure \"internet_server\".", LL_ERROR);
			exit(2);
		}
		else
		{
			Server->Log("Internet server not configured", LL_INFO);
			connected = false;
		}
	}

	std::string tmp;
	server_settings.internet_compress=true;
	if(settings->getValue("internet_compress", &tmp) || settings->getValue("internet_compress_def", &tmp) )
	{
		if(tmp=="false")
			server_settings.internet_compress=false;
	}
	server_settings.internet_encrypt=true;
	if(settings->getValue("internet_encrypt", &tmp) || settings->getValue("internet_encrypt_def", &tmp) )
	{
		if(tmp=="false")
			server_settings.internet_encrypt=false;
	}
	std::string internet_connect_always_str;
	if(settings->getValue("internet_connect_always", &tmp) || settings->getValue("internet_connect_always_def", &tmp) )
	{
		if(tmp=="true")
		{
			server_settings.internet_connect_always=true;
		}
		else
		{
			server_settings.internet_connect_always=false;
		}
	}
	Server->destroy(settings);
}

bool InternetClient::tryToConnect(IScopedLock *lock)
{
	if(server_settings.servers.empty())
	{
		setStatusMsg("no_server");
		return false;
	}

	for(size_t i=0;i<server_settings.servers.size();++i)
	{
		SServerConnectionSettings selected_server_settings = server_settings.servers[i];

		lock->relock(nullptr);
		Server->Log("Trying to connect to internet server "+ formatServerForLog(selected_server_settings), LL_DEBUG);
		std::unique_ptr<CTCPStack> tcpstack(new CTCPStack(true));
		IPipe *cs = connect(selected_server_settings, *tcpstack);
		lock->relock(mutex);
		if(cs!=nullptr)
		{
			server_settings.selected_server=i;
			Server->Log("Successfully connected.", LL_DEBUG);
			setStatusMsg("connected");
			Server->getThreadPool()->execute(new InternetClientThread(this, cs, server_settings, tcpstack.release()), "internet client");
			newConnection();
			return true;
		}
	}

	setStatusMsg("connecting_failed");
	Server->Log("Connecting failed.", LL_DEBUG);
	return false;
}

std::vector<THREADPOOL_TICKET> InternetClient::start(bool use_pool)
{
	init_mutex();

	std::vector<SFile> facet_dirs = getFiles("urbackup");

	std::vector<THREADPOOL_TICKET> ret;
	for (SFile& fdir : facet_dirs)
	{
		if (!fdir.isdir || !next(fdir.name, 0, "data_"))
			continue;

		int facet_id = watoi(getafter("data_", fdir.name));
		std::string settings_fn = "urbackup/data_" + convert(facet_id) + "/settings.cfg";

		if (!use_pool)
		{
			Server->createThread(new InternetClient(facet_id, settings_fn), "internet client main");
		}
		else
		{
			THREADPOOL_TICKET tt= Server->getThreadPool()->execute(new InternetClient(facet_id, settings_fn), "internet client main");
			ret.push_back(tt);
		}
	}	
	return ret;
}

void InternetClient::stop(std::vector<THREADPOOL_TICKET> tt)
{
	{
		IScopedLock lock(g_mutex);
		do_exit=true;

		for(auto it: running_client_facets)
			it.second->notify_all();
	}

	if (!tt.empty())
	{
		Server->getThreadPool()->waitFor(tt);
		destroy_mutex();
	}
}

void InternetClient::addOnetimeToken(const std::string &token)
{
	if(token.size()<=sizeof(unsigned int)+1)
		return;

	unsigned int token_id;
	std::string token_str;

	memcpy(&token_id, token.data(), sizeof(unsigned int));
	token_str.resize(token.size()-sizeof(unsigned int));
	memcpy((char*)token_str.data(), token.data()+sizeof(unsigned int), token.size()-sizeof(unsigned int));

	IScopedLock lock(onetime_token_mutex);
	
	onetime_tokens.push(std::pair<unsigned int, std::string>(token_id, token_str) );
}

std::pair<unsigned int, std::string> InternetClient::getOnetimeToken(void)
{
	IScopedLock lock(onetime_token_mutex);
	if(!onetime_tokens.empty())
	{
		std::pair<unsigned int, std::string> ret=onetime_tokens.front();
		onetime_tokens.pop();
		return ret;
	}
	else
	{
		return std::pair<unsigned int, std::string>(0, std::string() );
	}
}


void InternetClient::clearOnetimeTokens()
{
	IScopedLock lock(onetime_token_mutex);
	while(!onetime_tokens.empty())
	{
		onetime_tokens.pop();
	}
}

std::string InternetClient::getStatusMsg(int facet_id)
{
	IScopedLock lock(g_mutex);
	return status_msgs[facet_id];
}

void InternetClient::setStatusMsg(const std::string& msg)
{
	IScopedLock lock(g_mutex);
	status_msgs[facet_id]=msg;
}

InternetClientThread::InternetClientThread(InternetClient* internet_client, IPipe *cs, const SServerSettings &server_settings, CTCPStack* tcpstack)
	: internet_client(internet_client), cs(cs), server_settings(server_settings), tcpstack(tcpstack)
{
	if (this->tcpstack == nullptr)
		this->tcpstack = new CTCPStack(true);
}

InternetClientThread::~InternetClientThread()
{
	delete tcpstack;
}

char *InternetClientThread::getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms)
{
	int64 starttime=Server->getTimeMS();
	char *buf = tcpstack->getPacket(&replysize);
	if (buf != nullptr)
		return buf;

	while(Server->getTimeMS()-starttime<timeoutms)
	{
		std::string ret;
		size_t rc=pipe->Read(&ret, timeoutms);
		if(rc==0)
		{
			return nullptr;
		}
		tcpstack->AddData((char*)ret.c_str(), ret.size());
		buf=tcpstack->getPacket(&replysize);
		if(buf!=nullptr)
			return buf;
	}
	return nullptr;
}

void InternetClientThread::operator()(void)
{
	bool finish_ok=false;
	bool rm_connection=true;

	if(cs==nullptr)
	{
		int tries=10;
		while(tries>0 && cs==nullptr)
		{
			cs = InternetClient::connect(server_settings.servers[server_settings.selected_server], *tcpstack);
			--tries;
			internet_client->setStatusMsg("connecting_failed");
			if(cs==nullptr && tries>0)
			{
				Server->Log("Connecting to server "+ formatServerForLog(server_settings.servers[server_settings.selected_server]) + " failed. Retrying in 30s...", LL_INFO);
				Server->wait(30000);
			}
		}
		if(cs==nullptr)
		{
			Server->Log("Connecting to server "+ formatServerForLog(server_settings.servers[server_settings.selected_server])
				+ " failed", LL_INFO);
			internet_client->rmConnection();
			internet_client->setHasConnection(false);
			internet_client->setStatusMsg("connecting_failed");
			return;
		}
		else
		{
			internet_client->setStatusMsg("connected");
		}
	}

	IPipe *comm_pipe=nullptr;
	IPipe *comp_pipe=nullptr;

	std::string challenge;
	unsigned int server_capa;
	unsigned int capa=0;
	int compression_level=6;
	unsigned int server_iterations;
	std::string authkey;
	std::string challenge_response;
	InternetServicePipe2* ics_pipe = new InternetServicePipe2();
	std::string hmac_key;
	std::string server_pubkey;
	bool destroy_cs = true;

	struct SDelBuf {
		SDelBuf(char* b) : b(b) {}
		~SDelBuf() { delete[] b; }
		char* b;
	};

	{
		char *buf;
		size_t bufsize;
		buf=getReply(tcpstack, cs, bufsize, ic_auth_timeout);	
		if(buf==nullptr)
		{
			Server->Log("Error receiving challenge packet");
			goto cleanup;
		}
		SDelBuf delBuf(buf);
		CRData rd(buf, bufsize);
		char id;
		if(!rd.getChar(&id)) 
		{
			Server->Log("Error reading id of challenge packet");
			goto cleanup;
		}
		if(id==ID_ISC_CHALLENGE)
		{
			if(!( rd.getStr(&challenge)
				&& rd.getUInt(&server_capa)
				&& rd.getInt(&compression_level)
				&& rd.getUInt(&server_iterations) ))
			{
				std::string error = "Not enough challenge fields -1";
				Server->Log(error, LL_ERROR);
				internet_client->setStatusMsg("error:"+error);
				goto cleanup;
			}

			if(!rd.getStr(&server_pubkey) || server_pubkey.empty())
			{
				std::string error = "No server public key. Server version probably not new enough.";
				Server->Log(error, LL_ERROR);
				internet_client->setStatusMsg("error:"+error);
				goto cleanup;
			}

			if(challenge.size()<32)
			{
				std::string error = "Challenge not long enough -1";
				Server->Log(error, LL_ERROR);
				internet_client->setStatusMsg("error:"+error);
				goto cleanup;
			}
		}
		else
		{
			std::string error = "Unknown response id -2";
			Server->Log(error, LL_ERROR);
			internet_client->setStatusMsg("error:"+error);
			goto cleanup;
		}
	}
	
	{
		std::pair<unsigned int, std::string> token=internet_client->getOnetimeToken();

		CWData data;
		if(token.second.empty())
		{
			data.addChar(ID_ISC_AUTH2);
		}
		else
		{
			data.addChar(ID_ISC_AUTH_TOKEN2);
		}

		data.addString(server_settings.clientname);

		std::string client_challenge=generateRandomBinaryAuthKey();

		if(token.second.empty())
		{
			std::unique_ptr<IECDHKeyExchange> ecdh_key_exchange(crypto_fak->createECDHKeyExchange());

			std::string shared_key = ecdh_key_exchange->getSharedKey(server_pubkey);
			if (shared_key.empty())
			{
				Server->Log("Error calculating ECDH shared key from server public key", LL_ERROR);
				goto cleanup;
			}
			authkey=server_settings.authkey;
			std::string salt = challenge+client_challenge+ shared_key;
			hmac_key=crypto_fak->generateBinaryPasswordHash(authkey, salt, (std::max)(pbkdf2_iterations,server_iterations) );
			std::string hmac_l=crypto_fak->generateBinaryPasswordHash(hmac_key, challenge, 1);
			data.addString(hmac_l);
			data.addString(ecdh_key_exchange->getPublicKey());
		}
		else
		{
			authkey=token.second;
			hmac_key=crypto_fak->generateBinaryPasswordHash(authkey, challenge+client_challenge, 1 );
			std::string hmac_l=crypto_fak->generateBinaryPasswordHash(hmac_key, challenge, 1);
			data.addString(hmac_l);
			data.addUInt(token.first);
		}
		
		data.addString(client_challenge);
		data.addUInt(pbkdf2_iterations);
		tcpstack->Send(cs, data);

		challenge_response=crypto_fak->generateBinaryPasswordHash(hmac_key, client_challenge, 1);
	}

	{
		char *buf;
		size_t bufsize;
		buf=getReply(tcpstack, cs, bufsize, ic_auth_timeout);	
		if(buf==nullptr)
		{
			Server->Log("Error receiving authentication response");
			goto cleanup;
		}
		
		SDelBuf delBuf(buf);
		CRData rd(buf, bufsize);
		char id;
		if(!rd.getChar(&id)) 
		{
			Server->Log("Error reading id of authentication response");
			goto cleanup;
		}
		if(id==ID_ISC_AUTH_FAILED)
		{
			std::string errmsg="None";
			rd.getStr(&errmsg);
			int loglevel = LL_ERROR;
			if(errmsg=="Token not found")
			{
				internet_client->clearOnetimeTokens();
				loglevel=LL_INFO;
				internet_client->setStatusMsg("error:Temporary authentication failure: "+errmsg);
			}
			else
			{
				internet_client->setStatusMsg("error:Authentication failure: "+errmsg);
			}
			Server->Log("Internet server auth failed. Error: "+errmsg, loglevel);
			
			goto cleanup;
		}
		else if(id!=ID_ISC_AUTH_OK)
		{
			std::string error = "Unknown response id -1";
			Server->Log(error, LL_ERROR);
			internet_client->setStatusMsg("error:"+error);
			goto cleanup;
		}
		else
		{
			std::string hmac;
			rd.getStr(&hmac);
			if(hmac!=challenge_response)
			{
				std::string error = "Server authentification failed";
				Server->Log(error, LL_ERROR);
				internet_client->setStatusMsg("error:"+error);
				goto cleanup;
			}

			ics_pipe->init(cs, hmac_key);

			std::string new_token;
			while(rd.getStr(&new_token))
			{
				internet_client->addOnetimeToken(ics_pipe->decrypt(new_token));
			}
		}
	}

	{
		CWData data;
		data.addChar(ID_ISC_CAPA);

		if(server_settings.internet_encrypt )
			capa|=IPC_ENCRYPTED;

		if (server_settings.internet_compress)
		{
			if (server_capa & IPC_COMPRESSED)
				capa |= IPC_COMPRESSED;
#ifndef NO_ZSTD_COMPRESSION
			if (server_capa & IPC_COMPRESSED_ZSTD)
				capa |= IPC_COMPRESSED_ZSTD;
#endif
		}

		data.addUInt(capa);

		tcpstack->Send(ics_pipe, data);
	}

	comm_pipe=cs;
	
	if( capa & IPC_ENCRYPTED )
	{
		ics_pipe->setBackendPipe(comm_pipe);
		comm_pipe=ics_pipe;
	}
#ifndef NO_ZSTD_COMPRESSION
	if (capa & IPC_COMPRESSED_ZSTD)
	{
		comp_pipe = new CompressedPipeZstd(comm_pipe, compression_level, -1);
		comm_pipe = comp_pipe;
	}
	else
#endif
	if( capa & IPC_COMPRESSED )
	{
		comp_pipe=new CompressedPipe2(comm_pipe, compression_level);
		comm_pipe=comp_pipe;
	}

	finish_ok=true;
	internet_client->resetAuthErr();

	while(true)
	{
		char *buf;
		size_t bufsize;

		unsigned int ping_timeout;
		if (next(server_settings.clientname, 0, "##restore##"))
		{
			ping_timeout = ic_restore_ping_timeout;
		}
		else if(ClientConnector::isBackupRunning())
		{
			ping_timeout=ic_backup_running_ping_timeout;
		}
		else
		{
			ping_timeout=ic_ping_timeout;
		}

		buf=getReply(tcpstack, comm_pipe, bufsize, ping_timeout);
		if(buf==nullptr)
		{
			goto cleanup;
		}

		SDelBuf delBuf(buf);
		CRData rd(buf, bufsize);
		char id;
		if(!rd.getChar(&id)) 
		{
			goto cleanup;
		}
		if(id==ID_ISC_PING)
		{
			CWData data;
			data.addChar(ID_ISC_PONG);
			tcpstack->Send(comm_pipe, data);
		}
		else if(id==ID_ISC_CONNECT)
		{
			char service=0;
			rd.getChar(&service);

			if(service==SERVICE_COMMANDS || service==SERVICE_FILESRV)
			{
				CWData data;
				data.addChar(ID_ISC_CONNECT_OK);
				tcpstack->Send(comm_pipe, data);

				internet_client->rmConnection();
				rm_connection=false;
			}
			else
			{
				Server->Log("Client service not found", LL_ERROR);
				goto cleanup;
			}

			if(service==SERVICE_COMMANDS)
			{
				Server->Log("Started connection to SERVICE_COMMANDS", LL_DEBUG);
				ClientConnector clientservice;
				runServiceWrapper(comm_pipe, &clientservice);
				Server->Log("SERVICE_COMMANDS finished", LL_DEBUG);
				destroy_cs=clientservice.closeSocket();
				goto cleanup;
			}
			else if(service==SERVICE_FILESRV)
			{
				Server->Log("Started connection to SERVICE_FILESRV", LL_DEBUG);
				IndexThread::getFileSrv()->runClient(comm_pipe, nullptr);
				Server->Log("SERVICE_FILESRV finished", LL_DEBUG);
				goto cleanup;
			}
		}
		else
		{
			Server->Log("Unknown command id", LL_ERROR);
			goto cleanup;
		}
	}

cleanup:
	if(destroy_cs)
	{
		delete comp_pipe;
		if(cs!=nullptr)
			Server->destroy(cs);
		delete ics_pipe;
	}	
	if(!finish_ok)
	{
		internet_client->setHasAuthErr();
		Server->Log("InternetClient: Had an auth error");
	}
	if(rm_connection)
		internet_client->rmConnection();

	delete this;
}

void InternetClientThread::runServiceWrapper(IPipe *pipe, ICustomClient *client)
{
	client->Init(Server->getThreadID(), pipe, server_settings.servers[server_settings.selected_server].hostname);
	ClientConnector * cc=dynamic_cast<ClientConnector*>(client);
	if(cc!=nullptr)
	{
		cc->setIsInternetConnection();
	}
	while(true)
	{
		bool b=client->Run(nullptr);
		if(!b)
		{
			printInfo(pipe);
			return;
		}

		if(client->wantReceive())
		{
			if(pipe->isReadable(10))
			{
				client->ReceivePackets(nullptr);
			}
			else if(pipe->hasError())
			{
				client->ReceivePackets(nullptr);
				Server->wait(20);
			}
		}
		else
		{
			Server->wait(20);
		}
	}
}

void InternetClientThread::printInfo( IPipe * pipe )
{
	int64 transferred_bytes = pipe->getTransferedBytes();

	if(transferred_bytes>1*1024*1024) //1MB
	{
		Server->Log("Service finished. Transferred "+PrettyPrintBytes(transferred_bytes));

		IPipe* back_pipe= pipe;
		CompressedPipe2* comp_pipe = dynamic_cast<CompressedPipe2*>(pipe);

		if(comp_pipe!=nullptr)
		{
			back_pipe=comp_pipe->getRealPipe();
		}

		int64 enc_overhead=0;
		InternetServicePipe2* isp2 = dynamic_cast<InternetServicePipe2*>(back_pipe);
		if(isp2!=nullptr)
		{
			enc_overhead=isp2->getEncryptionOverheadBytes();
			Server->Log("Encryption overhead: "+PrettyPrintBytes(enc_overhead));
		}

		if(comp_pipe!=nullptr)
		{
			int64 uncompr_transferred = comp_pipe->getUncompressedReceivedBytes()+comp_pipe->getUncompressedSentBytes();
			Server->Log("Transferred uncompressed: "+PrettyPrintBytes(uncompr_transferred)+" (ratio: "+convert((float)uncompr_transferred/(transferred_bytes-enc_overhead))+")");
			Server->Log("Average sent paket size: "+PrettyPrintBytes(comp_pipe->getUncompressedSentBytes()/comp_pipe->getSentFlushes()));
		}
	}
}

IPipe * InternetClient::connect(const SServerConnectionSettings & selected_server_settings, CTCPStack& tcpstack)
{
#if defined(_WIN32) || defined(WITH_OPENSSL)
	std::string proxy = selected_server_settings.proxy;
	if (!proxy.empty())
	{
		bool ssl = false;
		if (next(proxy, 0, "http://"))
		{
			ssl = false;
			proxy = proxy.substr(7);
		}
		else if (next(proxy, 0, "https://"))
		{
			ssl = true;
			proxy = proxy.substr(8);
		}
		
		std::string authorization;
		if (proxy.find("@") != std::string::npos)
		{
			std::string udata = getuntil("@", proxy);
			proxy = getafter("@", proxy);

			std::string username = getuntil(":", udata);
			std::string password = getafter(":", udata);
			std::string adata = username + ":" + password;

			authorization = "Proxy-Authorization: Basic " + base64_encode(reinterpret_cast<const unsigned char*>(adata.c_str()), static_cast<unsigned int>(adata.size()))+"\r\n";
		}

		unsigned short port = ssl ? 443 : 80;
		if (proxy.find(":") != std::string::npos)
		{
			port = static_cast<unsigned short>(watoi(getafter(":", proxy)));
			proxy = getuntil(":", proxy);
		}

		IPipe* cs;
		if (ssl)
			cs = Server->ConnectSslStream(proxy, port, 10000);
		else
			cs = Server->ConnectStream(proxy, port, 10000);

		if (cs == NULL)
			return cs;

		std::string connect_data = "CONNECT " + selected_server_settings.hostname + ":" + convert(selected_server_settings.port) + " HTTP/1.1\r\n"
			+"Host: "+ selected_server_settings.hostname+"\r\n"
			+ authorization+"\r\n";

		if (!cs->Write(connect_data))
		{
			Server->destroy(cs);
			return NULL;
		}

		char buf[512];
		int64 starttime = Server->getTimeMS();
		int state = 0;
		std::string state_data;
		int http_code = 0;
		do
		{
			size_t rc = cs->Read(buf, sizeof(buf), 10000);

			if (rc == 0)
			{
				break;
			}

			Server->Log("Proxy buf size " + convert(rc) + " content: " + buf, LL_DEBUG);

			for (size_t i = 0; i < rc; ++i)
			{
				char ch = buf[i];
				switch (state)
				{
				case 0:
				{
					if (ch == ' ')
					{
						if (strlower(state_data) != "http/1.1"
							&& strlower(state_data) != "http/1.0")
						{
							Server->Log("Unknown HTTP protocol: " + state_data, LL_ERROR);
							Server->destroy(cs);
							return NULL;
						}
						state_data.clear();
						++state;
					}
					else
						state_data += ch;
				}break;
				case 1:
				{
					if (ch == ' ')
					{
						http_code = watoi(state_data);
						state_data.clear();
						++state;
					}
					else
						state_data += ch;
				}break;
				case 2:
				{
					if (ch == '\n')
					{
						if (http_code != 200)
						{
							Server->Log("HTTP proxy returned error code " + convert(http_code) + " message \"" + state_data + "\"", LL_ERROR);
							Server->destroy(cs);
							return NULL;
						}
						state_data.clear();
						++state;
					}
					else if (ch != '\r')
					{
						state_data += ch;
					}
				} break;
				case 3:
				{
					if (ch == '\n')
					{
						if (i + 1 < rc)
						{
							Server->Log("Remaining buf pos " + convert(i) + " is " + convert(rc - i - 1), LL_DEBUG);
							tcpstack.AddData(&buf[i + 1], rc - i - 1);
						}
						else
						{
							Server->Log("No remaining buf", LL_DEBUG);
						}
						return cs;
					}
					else if (ch != '\r')
					{
						++state;
					}
				} break;
				case 4:
				{
					if (ch == '\n')
					{
						state = 3;
					}
				}break;
				default:
				{
					assert(false);
					Server->destroy(cs);
					return NULL;
				}break;
				}
			}

		} while (Server->getTimeMS() - starttime < 10000);

		Server->Log("Timeout connecting via http proxy");

		Server->destroy(cs);
		return NULL;
	}
	else if(next(selected_server_settings.hostname, 0, "ws://") ||
		next(selected_server_settings.hostname, 0, "wss://") )
	{
		std::string hostname;
		bool ssl;
		if (next(selected_server_settings.hostname, 0, "ws://"))
		{
			ssl = false;
			hostname = selected_server_settings.hostname.substr(5);
		}
		else
		{
			ssl = true;
			hostname = selected_server_settings.hostname.substr(6);
		}

		std::string authorization;
		if (hostname.find("@") != std::string::npos)
		{
			std::string udata = getuntil("@", hostname);
			hostname = getafter("@", hostname);

			std::string username = getuntil(":", udata);
			std::string password = getafter(":", udata);
			std::string adata = username + ":" + password;

			authorization = "Authorization: Basic " + base64_encode(reinterpret_cast<const unsigned char*>(adata.c_str()), static_cast<unsigned int>(adata.size())) + "\r\n";
		}

		unsigned short port = ssl ? 443 : 80;
		std::string loc;
		if (hostname.find(":") != std::string::npos)
		{
			std::string port_str = getafter(":", hostname);
			if(port_str.find("/")!=std::string::npos)
			{
				port = static_cast<unsigned short>(watoi(getuntil("/", port_str)));
				loc = getafter("/", port_str);
			}
			else
			{
				port = static_cast<unsigned short>(watoi(port_str));
			}
			
			hostname = getuntil(":", hostname);
		}
		else if (hostname.find("/") != std::string::npos)
		{
			loc = getafter("/", hostname);
			hostname = getuntil("/", hostname);
		}

		if (loc.empty() || 
			loc[0] != '/')
			loc = "/" + loc;

		IPipe* cs;
		if (ssl)
			cs = Server->ConnectSslStream(hostname, port, 10000);
		else
			cs = Server->ConnectStream(hostname, port, 10000);

		if (cs == NULL)
			return cs;

		unsigned char websocket_key[16];
		Server->secureRandomFill(reinterpret_cast<char*>(websocket_key), sizeof(websocket_key));

		std::string websocket_key_str = base64_encode(websocket_key, sizeof(websocket_key));

		cs->Write(std::string("GET ")+loc+" HTTP/1.1\r\n"
			"Host: "+hostname+"\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Key: "+ websocket_key_str+"\r\n"
			"Sec-WebSocket-Protocol: urbackup\r\n"
			"Sec-WebSocket-Version: 13\r\n\r\n");

		char buf[512];

		int64 resp_timeout = 30000;

		int64 starttime = Server->getTimeMS();
		int state = 0;
		std::string header;
		std::string pipe_add;
		bool has_header = false;
		do
		{
			size_t read = cs->Read(buf, sizeof(buf), 10000);

			if (read > 0)
			{
				for (size_t i = 0; i < read; ++i)
				{
					char ch = buf[i];
					if (state == 0)
					{
						if (ch == '\r')
							state = 1;
						else if (ch == '\n')
							state = 2;
						else
							state = 0;
					}
					else if (state == 1)
					{
						if (ch == '\n')
							state = 2;
						else
							state = 0;
					}
					else if (state == 2)
					{
						if (ch == '\r')
							state = 3;
						else if (ch == '\n')
							state = 4;
						else
							state = 0;
					}
					else if (state == 3)
					{
						if (ch == '\n')
							state = 4;
						else
							state = 0;
					}
					else if (state == 4)
					{
						pipe_add.assign(buf + i, read - i);
						has_header = true;
						break;
					}

					header += ch;
				}

				if (state == 4)
				{
					has_header = true;
				}
			}
		} while (!has_header &&
			Server->getTimeMS() - starttime < resp_timeout);


		if (!has_header)
		{
			Server->Log("Timeout connecting via web socket");
			Server->destroy(cs);
			return NULL;
		}

		if (!next(header, 0, "HTTP/1.1 101"))
		{
			Server->Log("Error connecting via web socket. Response: " + header, LL_ERROR);
			Server->destroy(cs);
			return NULL;
		}

		std::vector<std::string> headers;
		Tokenize(getafter("\n", header), headers, "\n");
		str_map header_map;
		for (size_t i = 0; i < headers.size(); ++i)
		{
			std::string key = strlower(getuntil(":", headers[i]));
			header_map[key] = trim(getafter(":", headers[i]));
		}

		if (header_map["sec-websocket-protocol"] != "urbackup")
		{
			Server->Log("Unknown web socket protocol \"" + header_map["sec-websocket-protocol"] + "\"", LL_ERROR);
			Server->destroy(cs);
			return NULL;
		}

		if (header_map["upgrade"] != "websocket")
		{
			Server->Log("Unknown web socket upgrade value \"" + header_map["upgrade"] + "\"", LL_ERROR);
			Server->destroy(cs);
			return NULL;
		}

		if (strlower(header_map["connection"]) != "upgrade")
		{
			Server->Log("Unknown web socket connection value \"" + header_map["connection"] + "\"", LL_ERROR);
			Server->destroy(cs);
			return NULL;
		}

		std::string accept_key = header_map["sec-websocket-accept"];

		std::string expected_accept_key = websocket_key_str + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

		std::string sha_bin = crypto_fak->sha1Binary(expected_accept_key);

		expected_accept_key = base64_encode(reinterpret_cast<const unsigned char*>(sha_bin.data()), static_cast<unsigned int>(sha_bin.size()));

		if (accept_key != expected_accept_key)
		{
			Server->Log("Web socket accept key wrong. Expected " + expected_accept_key + " got "+accept_key, LL_ERROR);
			Server->destroy(cs);
			return NULL;
		}

		return new WebSocketPipe(cs, true, false, pipe_add, true);
	}
#endif
	
	return Server->ConnectStream(selected_server_settings.hostname,
		selected_server_settings.port, 10000);
}