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
#include "../urbackupcommon/CompressedPipe.h"

#include "../stringtools.h"

#include <stdlib.h>
#include <memory.h>

#include "../cryptoplugin/ICryptoFactory.h"

extern ICryptoFactory *crypto_fak;
const unsigned int pbkdf2_iterations=20000;

IMutex *InternetClient::mutex=NULL;
bool InternetClient::connected=NULL;
size_t InternetClient::n_connections=0;
int64 InternetClient::last_lan_connection=0;
bool InternetClient::update_settings=false;
SServerSettings InternetClient::server_settings;
ICondition *InternetClient::wakeup_cond=NULL;
int InternetClient::auth_err=0;
std::queue<std::pair<unsigned int, std::string> > InternetClient::onetime_tokens;
bool InternetClient::do_exit=false;
IMutex *InternetClient::onetime_token_mutex=NULL;
std::string InternetClient::status_msg="initializing";


const unsigned int ic_lan_timeout=10*60*1000;
const unsigned int spare_connections=1;
const unsigned int ic_auth_timeout=60000;
const unsigned int ic_ping_timeout=31*60*1000;
const unsigned int ic_backup_running_ping_timeout=60*1000;
const int ic_sleep_after_auth_errs=2;

const char SERVICE_COMMANDS=0;
const char SERVICE_FILESRV=1;

void InternetClient::init_mutex(void)
{
	mutex=Server->createMutex();
	wakeup_cond=Server->createCondition();
	onetime_token_mutex=Server->createMutex();
}

void InternetClient::destroy_mutex(void)
{
	Server->destroy(mutex);
	Server->destroy(wakeup_cond);
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
	IScopedLock lock(mutex);
	last_lan_connection=Server->getTimeMS();
}

int64 InternetClient::timeSinceLastLanConnection()
{
	int64 ctime=Server->getTimeMS();
	IScopedLock lock(mutex);

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
	IScopedLock lock(mutex);
	return connected;
}

void InternetClient::setHasConnection(bool b)
{
	IScopedLock lock(mutex);
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
	IScopedLock lock(mutex);
	update_settings=true;
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
	Server->waitForStartupComplete();

	setStatusMsg("wait_local");
	if(Server->getServerParameter("internet_only_mode")!="true")
	{
		const size_t wait_time_ms=180000;
		Server->Log("Internet only mode not enabled. Waiting for local server for "+FormatTime(wait_time_ms/1000)+"...", LL_DEBUG);
		Server->wait(wait_time_ms);
	}
	else
	{
		Server->wait(1000);
	}
	doUpdateSettings();
	while(!do_exit)
	{
		IScopedLock lock(mutex);
		if(update_settings)
		{
			doUpdateSettings();
			update_settings=false;
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
					Server->getThreadPool()->execute(new InternetClientThread(NULL, server_settings));
					newConnection();
				}
				else
				{
					wakeup_cond->wait(&lock);
					if(auth_err>=ic_sleep_after_auth_errs)
					{
						lock.relock(NULL);
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

	ISettingsReader *settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");
	if(settings==NULL)
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
			Server->Log("Internet mode is not enabled.", LL_DEBUG);
			return;
		}
	}

	std::string server_name;
	std::string computername;
	std::string server_port="55415";
	std::string authkey;
	if(!settings->getValue("internet_authkey", &authkey) && !settings->getValue("internet_authkey_def", &authkey))
	{
		Server->destroy(settings);
		Server->Log("Cannot read internet_authkey", LL_INFO);
		return;
	}
	if( (!settings->getValue("computername", &computername)
		 && !settings->getValue("computername_def", &computername) ) 
		   || computername.empty())
	{
		computername=Server->ConvertToUTF8(IndexThread::getFileSrv()->getServerName());
	}
	if(settings->getValue("internet_server", &server_name) || settings->getValue("internet_server_def", &server_name) )
	{
		if(!settings->getValue("internet_server_port", &server_port) )
			settings->getValue("internet_server_port_def", &server_port);

		std::vector<std::string> server_names;
		Tokenize(server_name, server_names, ";");

		std::vector<std::string> server_ports;
		Tokenize(server_port, server_ports, ";");

		for(size_t i=0;i<server_names.size();++i)
		{
			if(i<server_ports.size())
			{
				server_settings.servers.push_back(std::make_pair(server_names[i],
					static_cast<unsigned short>(atoi(server_ports[i].c_str()))));
			}
			else
			{
				server_settings.servers.push_back(std::make_pair(server_names[i],
					static_cast<unsigned short>(atoi(server_ports[server_ports.size()-1].c_str()))));
			}
		}
		server_settings.clientname=computername;
		server_settings.authkey=authkey;
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
		std::string name=server_settings.servers[i].first;

		unsigned short port=server_settings.servers[i].second;

		lock->relock(NULL);
		Server->Log("Trying to connect to internet server \""+name+"\" at port "+nconvert(port), LL_DEBUG);
		IPipe *cs=Server->ConnectStream(name, port, 10000);
		lock->relock(mutex);
		if(cs!=NULL)
		{
			server_settings.selected_server=i;
			Server->Log("Successfully connected.", LL_DEBUG);
			setStatusMsg("connected");
			Server->getThreadPool()->execute(new InternetClientThread(cs, server_settings));
			newConnection();
			return true;
		}
	}

	setStatusMsg("connecting_failed");
	Server->Log("Connecting failed.", LL_DEBUG);
	return false;
}

THREADPOOL_TICKET InternetClient::start(bool use_pool)
{
	init_mutex();
	if(!use_pool)
	{
		Server->createThread(new InternetClient);
		return ILLEGAL_THREADPOOL_TICKET;
	}
	else
	{
		return Server->getThreadPool()->execute(new InternetClient);
	}
}

void InternetClient::stop(THREADPOOL_TICKET tt)
{
	{
		IScopedLock lock(mutex);
		do_exit=true;
		wakeup_cond->notify_all();
	}

	if(tt==ILLEGAL_THREADPOOL_TICKET)
		Server->wait(1000);
	else
		Server->getThreadPool()->waitFor(tt);

	destroy_mutex();
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

std::string InternetClient::getStatusMsg()
{
	IScopedLock lock(mutex);
	return status_msg;
}

void InternetClient::setStatusMsg(const std::string& msg)
{
	IScopedLock lock(mutex);
	status_msg=msg;
}

InternetClientThread::InternetClientThread(IPipe *cs, const SServerSettings &server_settings)
	: cs(cs), server_settings(server_settings)
{
}

char *InternetClientThread::getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms)
{
	int64 starttime=Server->getTimeMS();
	char *buf;
	while(Server->getTimeMS()-starttime<timeoutms)
	{
		std::string ret;
		size_t rc=pipe->Read(&ret, timeoutms);
		if(rc==0)
		{
			return NULL;
		}
		tcpstack->AddData((char*)ret.c_str(), ret.size());
		buf=tcpstack->getPacket(&replysize);
		if(buf!=NULL)
			return buf;
	}
	return NULL;
}

void InternetClientThread::operator()(void)
{
	CTCPStack tcpstack(true);
	bool finish_ok=false;
	bool rm_connection=true;

	if(cs==NULL)
	{
		int tries=10;
		while(tries>0 && cs==NULL)
		{
			cs=Server->ConnectStream(server_settings.servers[server_settings.selected_server].first,
				server_settings.servers[server_settings.selected_server].second, 10000);
			--tries;
			InternetClient::setStatusMsg("connecting_failed");
			if(cs==NULL && tries>0)
			{
				Server->Log("Connecting to server "+server_settings.servers[server_settings.selected_server].first
					+ " failed. Retrying in 30s...", LL_INFO);
				Server->wait(30000);
			}
		}
		if(cs==NULL)
		{
			Server->Log("Connecting to server "+server_settings.servers[server_settings.selected_server].first
				+ " failed", LL_INFO);
			InternetClient::rmConnection();
			InternetClient::setHasConnection(false);
			InternetClient::setStatusMsg("connecting_failed");
			return;
		}
		else
		{
			InternetClient::setStatusMsg("connected");
		}
	}

	IPipe *comm_pipe=NULL;
	IPipe *comp_pipe=NULL;

	std::string challenge;
	unsigned int server_capa;
	unsigned int capa=0;
	int compression_level;
	unsigned int server_iterations;
	std::string authkey;
	std::string challenge_response;
	InternetServicePipe2* ics_pipe = new InternetServicePipe2();
	std::string hmac_key;
	std::string server_pubkey;
	bool destroy_cs = true;
	std::auto_ptr<IECDHKeyExchange> ecdh_key_exchange(crypto_fak->createECDHKeyExchange());

	{
		char *buf;
		size_t bufsize;
		buf=getReply(&tcpstack, cs, bufsize, ic_auth_timeout);	
		if(buf==NULL)
		{
			goto cleanup;
		}
		CRData rd(buf, bufsize);
		char id;
		if(!rd.getChar(&id)) 
		{
			delete []buf;
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
				InternetClient::setStatusMsg("error:"+error);
				delete []buf;
				goto cleanup;
			}

			if(!rd.getStr(&server_pubkey) || server_pubkey.empty())
			{
				std::string error = "No server public key. Server version probably not new enough.";
				Server->Log(error, LL_ERROR);
				InternetClient::setStatusMsg("error:"+error);
				delete []buf;
				goto cleanup;
			}

			if(challenge.size()<32)
			{
				std::string error = "Challenge not long enough -1";
				Server->Log(error, LL_ERROR);
				InternetClient::setStatusMsg("error:"+error);
				delete []buf;
				goto cleanup;
			}
		}
		else
		{
			std::string error = "Unknown response id -2";
			Server->Log(error, LL_ERROR);
			InternetClient::setStatusMsg("error:"+error);
			delete []buf;
			goto cleanup;
		}

		delete []buf;
	}
	
	{
		std::pair<unsigned int, std::string> token=InternetClient::getOnetimeToken();

		CWData data;
		if(token.second.empty())
		{
			data.addChar(ID_ISC_AUTH2);
		}
		else
		{
			data.addChar(ID_ISC_AUTH_TOKEN);
		}

		data.addString(server_settings.clientname);

		std::string client_challenge=generateRandomBinaryAuthKey();

		if(token.second.empty())
		{
			authkey=server_settings.authkey;			
			std::string salt = challenge+client_challenge+ecdh_key_exchange->getSharedKey(server_pubkey);
			hmac_key=crypto_fak->generateBinaryPasswordHash(authkey, salt, (std::max)(pbkdf2_iterations,server_iterations) );
			std::string hmac_l=crypto_fak->generateBinaryPasswordHash(hmac_key, challenge, 1);
			data.addString(hmac_l);
		}
		else
		{
			authkey=token.second;
			hmac_key=crypto_fak->generateBinaryPasswordHash(authkey, challenge+client_challenge, 1 );
			std::string hmac_l=crypto_fak->generateBinaryPasswordHash(hmac_key, challenge, 1);
			data.addString(hmac_l);
			data.addUInt(token.first);
		}

		ecdh_key_exchange.reset();

		
		data.addString(client_challenge);
		data.addUInt(pbkdf2_iterations);
		tcpstack.Send(cs, data);

		challenge_response=crypto_fak->generateBinaryPasswordHash(hmac_key, client_challenge, 1);
	}

	{
		char *buf;
		size_t bufsize;
		buf=getReply(&tcpstack, cs, bufsize, ic_auth_timeout);	
		if(buf==NULL)
		{
			goto cleanup;
		}
		CRData rd(buf, bufsize);
		char id;
		if(!rd.getChar(&id)) 
		{
			delete []buf;
			goto cleanup;
		}
		if(id==ID_ISC_AUTH_FAILED)
		{
			std::string errmsg="None";
			rd.getStr(&errmsg);
			int loglevel = LL_ERROR;
			if(errmsg=="Token not found")
			{
				InternetClient::clearOnetimeTokens();
				loglevel=LL_INFO;
				InternetClient::setStatusMsg("error:Temporary authentication failure: "+errmsg);
			}
			else
			{
				InternetClient::setStatusMsg("error:Authentication failure: "+errmsg);
			}
			Server->Log("Internet server auth failed. Error: "+errmsg, loglevel);
			
			delete []buf;
			goto cleanup;
		}
		else if(id!=ID_ISC_AUTH_OK)
		{
			std::string error = "Unknown response id -1";
			Server->Log(error, LL_ERROR);
			InternetClient::setStatusMsg("error:"+error);
			delete []buf;
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
				InternetClient::setStatusMsg("error:"+error);
				delete []buf;
				goto cleanup;
			}

			ics_pipe->init(cs, hmac_key);

			std::string new_token;
			while(rd.getStr(&new_token))
			{
				InternetClient::addOnetimeToken(ics_pipe->decrypt(new_token));
			}
		}
	}

	{
		CWData data;
		data.addChar(ID_ISC_CAPA);

		if(server_settings.internet_encrypt )
			capa|=IPC_ENCRYPTED;

		if(server_settings.internet_compress && server_capa & IPC_COMPRESSED )
			capa|=IPC_COMPRESSED;

		data.addUInt(capa);

		tcpstack.Send(ics_pipe, data);
	}

	comm_pipe=cs;
	
	if( capa & IPC_ENCRYPTED )
	{
		ics_pipe->setBackendPipe(comm_pipe);
		comm_pipe=ics_pipe;
	}
	if( capa & IPC_COMPRESSED )
	{
		comp_pipe=new CompressedPipe(comm_pipe, compression_level);
		comm_pipe=comp_pipe;
	}

	finish_ok=true;
	InternetClient::resetAuthErr();

	while(true)
	{
		char *buf;
		size_t bufsize;

		unsigned int ping_timeout;
		if(ClientConnector::isBackupRunning())
		{
			ping_timeout=ic_backup_running_ping_timeout;
		}
		else
		{
			ping_timeout=ic_ping_timeout;
		}

		buf=getReply(&tcpstack, comm_pipe, bufsize, ping_timeout);
		if(buf==NULL)
		{
			goto cleanup;
		}

		CRData rd(buf, bufsize);
		char id;
		if(!rd.getChar(&id)) 
		{
			delete []buf;
			goto cleanup;
		}
		if(id==ID_ISC_PING)
		{
			delete []buf;
			CWData data;
			data.addChar(ID_ISC_PONG);
			tcpstack.Send(comm_pipe, data);
		}
		else if(id==ID_ISC_CONNECT)
		{
			char service=0;
			rd.getChar(&service);

			delete []buf;

			if(service==SERVICE_COMMANDS || service==SERVICE_FILESRV)
			{
				CWData data;
				data.addChar(ID_ISC_CONNECT_OK);
				tcpstack.Send(comm_pipe, data);

				InternetClient::rmConnection();
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
				IndexThread::getFileSrv()->runClient(comm_pipe);
				Server->Log("SERVICE_FILESRV finished", LL_DEBUG);
				goto cleanup;
			}
		}
		else
		{
			delete []buf;
			Server->Log("Unknown command id", LL_ERROR);
			goto cleanup;
		}
	}

cleanup:
	if(destroy_cs)
	{
		delete comp_pipe;
		if(cs!=NULL)
			Server->destroy(cs);
		delete ics_pipe;
	}	
	if(!finish_ok)
	{
		InternetClient::setHasAuthErr();
		Server->Log("InternetClient: Had an auth error");
	}
	if(rm_connection)
		InternetClient::rmConnection();

	delete this;
}

void InternetClientThread::runServiceWrapper(IPipe *pipe, ICustomClient *client)
{
	client->Init(Server->getThreadID(), pipe, server_settings.servers[server_settings.selected_server].first);
	ClientConnector * cc=dynamic_cast<ClientConnector*>(client);
	if(cc!=NULL)
	{
		cc->setIsInternetConnection();
	}
	while(true)
	{
		bool b=client->Run();
		if(!b)
		{
			return;
		}

		if(client->wantReceive())
		{
			if(pipe->isReadable(10))
			{
				client->ReceivePackets();
			}
			else if(pipe->hasError())
			{
				client->ReceivePackets();
				Server->wait(20);
			}
		}
		else
		{
			Server->wait(20);
		}
	}
}
