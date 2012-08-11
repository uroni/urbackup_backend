#include "InternetClient.h"

#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/SettingsReader.h"
#include "../Interface/ThreadPool.h"

#include "client.h"
#include "ClientService.h"

#include "../urbackupcommon/fileclient/data.h"
#include "../urbackupcommon/fileclient/tcpstack.h"
#include "../urbackupcommon/InternetServiceIDs.h"
#include "../urbackupcommon/InternetServicePipe.h"
#include "../urbackupcommon/internet_pipe_capabilities.h"
#include "../urbackupcommon/CompressedPipe.h"

#include <stdlib.h>

IMutex *InternetClient::mutex=NULL;
bool InternetClient::connected=NULL;
size_t InternetClient::n_connections=0;
unsigned int InternetClient::last_lan_connection=0;
bool InternetClient::update_settings=false;
SServerSettings InternetClient::server_settings;
ICondition *InternetClient::wakeup_cond=NULL;
int InternetClient::auth_err=0;

const unsigned int ic_lan_timeout=10*60*1000;
const unsigned int spare_connections=1;
const unsigned int ic_auth_timeout=60000;
const unsigned int ic_ping_timeout=31*60*1000;
const int ic_sleep_after_auth_errs=2;

const char SERVICE_COMMANDS=0;
const char SERVICE_FILESRV=1;

const std::string auth_text="##################URBACKUP--AUTH#########################-";

void InternetClient::init_mutex(void)
{
	mutex=Server->createMutex();
	wakeup_cond=Server->createCondition();
}

std::string InternetClientThread::generateRandomAuthKey(void)
{
	std::string rchars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	std::string key;
	for(int j=0;j<10;++j)
		key+=rchars[rand()%rchars.size()];
	return key;
}

void InternetClient::hasLANConnection(void)
{
	IScopedLock lock(mutex);
	last_lan_connection=Server->getTimeMS();
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
	Server->wait(60000);
	doUpdateSettings();
	while(true)
	{
		IScopedLock lock(mutex);
		if(update_settings)
		{
			doUpdateSettings();
			update_settings=false;
		}
		if(last_lan_connection==0 || Server->getTimeMS()-last_lan_connection>ic_lan_timeout)
		{
			if(!connected)
			{
				if(tryToConnect(&lock))
				{
					connected=true;
				}
				else
				{
					lock.relock(NULL);
					Server->wait(ic_lan_timeout/2);
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
			lock.relock(NULL);
			Server->wait(ic_lan_timeout);
		}
	}
}

void InternetClient::doUpdateSettings(void)
{
	server_settings.name.clear();

	ISettingsReader *settings=Server->createFileSettingsReader("urbackup/data/settings.cfg");
	if(settings==NULL)
		return;

	std::string internet_mode_enabled;
	if( !settings->getValue("internet_mode_enabled", &internet_mode_enabled) || internet_mode_enabled=="false" )
	{
		if( !settings->getValue("internet_mode_enabled_def", &internet_mode_enabled) || internet_mode_enabled=="false" )
		{
			Server->destroy(settings);
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
		return;
	}
	if(!settings->getValue("computername", &computername) || computername.empty())
	{
		computername=Server->ConvertToUTF8(IndexThread::getFileSrv()->getServerName());
	}
	if(settings->getValue("internet_server", &server_name) || settings->getValue("internet_server_def", &server_name) )
	{
		if(!settings->getValue("internet_server_port", &server_port) )
			settings->getValue("internet_server_port_def", &server_port);

		server_settings.name=server_name;
		server_settings.port=(unsigned short)atoi(server_port.c_str());
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
	Server->destroy(settings);
}

bool InternetClient::tryToConnect(IScopedLock *lock)
{
	std::string name=server_settings.name;
	if(name.empty())
		return false;

	unsigned short port=server_settings.port;
	lock->relock(NULL);
	IPipe *cs=Server->ConnectStream(name, port, 10000);
	lock->relock(mutex);
	if(cs!=NULL)
	{
		Server->getThreadPool()->execute(new InternetClientThread(cs, server_settings));
		newConnection();
		return true;
	}
	return false;
}

void InternetClient::start(void)
{
	init_mutex();
	Server->createThread(new InternetClient);
}

InternetClientThread::InternetClientThread(IPipe *cs, const SServerSettings &server_settings)
	: cs(cs), server_settings(server_settings)
{
}

char *InternetClientThread::getReply(CTCPStack *tcpstack, IPipe *pipe, size_t &replysize, unsigned int timeoutms)
{
	unsigned int starttime=Server->getTimeMS();
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
		cs=Server->ConnectStream(server_settings.name, server_settings.port, 10000);
		if(cs==NULL)
		{
			InternetClient::rmConnection();
			return;
		}
	}

	InternetServicePipe ics_pipe(cs, server_settings.authkey);

	IPipe *comm_pipe=NULL;
	IPipe *comp_pipe=NULL;

	std::string challenge;
	unsigned int server_capa;
	unsigned int capa=0;
	int compression_level;
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
			rd.getStr(&challenge);
			rd.getUInt(&server_capa);
			rd.getInt(&compression_level);
		}
		else
		{
			Server->Log("Unknown response id -2", LL_ERROR);
			delete []buf;
			goto cleanup;
		}

		delete []buf;
	}
	{
		CWData data;
		data.addChar(ID_ISC_AUTH);
		data.addString(server_settings.clientname);
		data.addString(ics_pipe.encrypt(auth_text+challenge) );
		challenge=generateRandomAuthKey();
		data.addString(challenge);
		tcpstack.Send(cs, data);
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
			Server->Log("Internet server auth failed. Error: "+errmsg, LL_ERROR);
			delete []buf;
			goto cleanup;
		}
		else if(id!=ID_ISC_AUTH_OK)
		{
			Server->Log("Unknown response id -1", LL_ERROR);
			delete []buf;
			goto cleanup;
		}
		else
		{
			std::string enc_d;
			rd.getStr(&enc_d);
			enc_d=ics_pipe.decrypt(enc_d);
			if(enc_d!=auth_text+challenge)
			{
				Server->Log("Server authentification failed", LL_ERROR);
				delete []buf;
				goto cleanup;
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

		tcpstack.Send(&ics_pipe, data);
	}

	comm_pipe=cs;
	
	if( capa & IPC_ENCRYPTED )
	{
		ics_pipe.setBackendPipe(comm_pipe);
		comm_pipe=&ics_pipe;
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
		buf=getReply(&tcpstack, comm_pipe, bufsize, ic_ping_timeout);
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
	delete comp_pipe;
	if(cs!=NULL)
		Server->destroy(cs);
	if(!finish_ok)
	{
		InternetClient::setHasAuthErr();
		Server->Log("InternetClient: Had an auth error");
	}
	if(rm_connection)
		InternetClient::rmConnection();
}

void InternetClientThread::runServiceWrapper(IPipe *pipe, ICustomClient *client)
{
	client->Init(Server->getThreadID(), pipe);
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