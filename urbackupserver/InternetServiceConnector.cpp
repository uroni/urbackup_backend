#include "InternetServiceConnector.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Database.h"
#include "../urbackupcommon/fileclient/data.h"
#include "../urbackupcommon/InternetServiceIDs.h"
#include "../urbackupcommon/InternetServicePipe.h"
#include "../urbackupcommon/CompressedPipe.h"
#include "server_settings.h"
#include "database.h"
#include "../stringtools.h"

const unsigned int ping_interval=5*60*1000;
const unsigned int ping_timeout=30000;
const unsigned int offline_timeout=ping_interval+10000;
const unsigned int establish_timeout=10000;

std::map<std::string, SClientData> InternetServiceConnector::client_data;
IMutex *InternetServiceConnector::mutex=NULL;

const std::string auth_text="##################URBACKUP--AUTH#########################-";

ICustomClient* InternetService::createClient()
{
	return new InternetServiceConnector;
}

void InternetService::destroyClient( ICustomClient * pClient)
{
	delete ((InternetServiceConnector*)pClient);
}

InternetServiceConnector::InternetServiceConnector(void)
{
	local_mutex=Server->createMutex();
}

InternetServiceConnector::~InternetServiceConnector(void)
{
	Server->destroy(local_mutex);
}

void InternetServiceConnector::Init(THREAD_ID pTID, IPipe *pPipe)
{
	tid=pTID;
	cs=pPipe;
	comm_pipe=cs;
	is_pipe=NULL;
	comp_pipe=NULL;
	do_connect=false;
	stop_connecting=false;
	is_connected=false;
	pinging=false;
	free_connection=false;
	state=ISS_AUTH;
	has_timeout=false;
	connection_done_cond=NULL;
	connection_stop_cond=NULL;
	tcpstack.reset();
	tcpstack.setAddChecksum(true);
	challenge=ServerSettings::generateRandomAuthKey();
	{
		CWData data;
		data.addChar(ID_ISC_CHALLENGE);
		data.addString(challenge);
		unsigned int capa=0;
		ServerSettings server_settings(Server->getDatabase(pTID, URBACKUPDB_SERVER));
		SSettings *settings=server_settings.getSettings();
		if(settings->internet_encrypt)
			capa|=IPC_ENCRYPTED;
		if(settings->internet_compress)
			capa|=IPC_COMPRESSED;

		compression_level=settings->internet_compression_level;
		data.addUInt(capa);
		data.addInt(compression_level);

		tcpstack.Send(cs, data);
	}
	lastpingtime=Server->getTimeMS();
}

void InternetServiceConnector::cleanup_pipes(void)
{
	delete is_pipe;
	is_pipe=NULL;
	delete comp_pipe;
	comp_pipe=NULL;
}

void InternetServiceConnector::cleanup(void)
{
	if(connection_done_cond!=NULL)
		Server->destroy(connection_done_cond);
}

bool InternetServiceConnector::Run(void)
{
	if(state==ISS_CONNECTING || state==ISS_USED)
	{
		if(stop_connecting)
		{
			connection_stop_cond->notify_all();
			cleanup_pipes();
			return false;
		}
		if(free_connection)
		{
			if(has_timeout)
				cleanup_pipes();

			return false;
		}
		return true;
	}
	
	if( has_timeout )
	{
		if(free_connection)
		{
			return false;
		}
		else
		{
			return true;
		}
	}

	if(state==ISS_QUIT)
	{
		Server->Log("state==ISS_QUIT Should not happen", LL_ERROR);
		return true;
	}

	if(do_connect && state==ISS_AUTHED )
	{
		CWData data;
		data.addChar(ID_ISC_CONNECT);
		data.addChar(target_service);
		tcpstack.Send(comm_pipe, data);
		Server->Log("Connecting to target service...", LL_DEBUG);
		state=ISS_CONNECTING;
		starttime=Server->getTimeMS();
	}

	if(state==ISS_AUTHED && Server->getTimeMS()-lastpingtime>ping_interval && !pinging)
	{
		lastpingtime=Server->getTimeMS();
		pinging=true;
		CWData data;
		data.addChar(ID_ISC_PING);
		tcpstack.Send(comm_pipe, data);
	}
	else if(state==ISS_AUTHED && pinging )
	{
		if(Server->getTimeMS()-lastpingtime>ping_timeout)
		{
			Server->Log("Ping timeout in InternetServiceConnector::Run", LL_DEBUG);
			IScopedLock lock(mutex);
			if(!do_connect)
			{
				has_timeout=true;
			}
		}
	}
	return true;
}

void InternetServiceConnector::ReceivePackets(void)
{
	std::string ret;
	size_t rc;
	rc=comm_pipe->Read(&ret);

	if(rc==0)
	{
		has_timeout=true;
		return;
	}

	tcpstack.AddData((char*)ret.c_str(), ret.size());

	char *buf;
	size_t packetsize;
	while((buf=tcpstack.getPacket(&packetsize))!=NULL)
	{
		CRData rd(buf, packetsize);
		char id;
		if(rd.getChar(&id))
		{
			switch(state)
			{
			case ISS_AUTH:
				{
					if(id==ID_ISC_AUTH)
					{
						std::string encdata;
						std::string errmsg;

						if(rd.getStr(&clientname) && rd.getStr(&encdata) )
						{
							IDatabase *db=Server->getDatabase(tid, URBACKUPDB_SERVER);
							IQuery *q=db->Prepare("SELECT value FROM settings_db.settings WHERE key='internet_authkey' AND clientid=(SELECT id FROM clients WHERE name=?)", false);
							q->Bind(clientname);
							db_results res=q->Read();
							db->destroyQuery(q);
							if(!res.empty())
							{
								
								authkey=Server->ConvertToUTF8(res[0][L"value"]);
								is_pipe=new InternetServicePipe(comm_pipe, authkey);			
								
								std::string auth_str=is_pipe->decrypt(encdata);
								if(auth_str==auth_text+challenge)
								{
									state=ISS_CAPA;
									comm_pipe=is_pipe;
								}
								else
								{
									Server->wait(1000);
									errmsg="Auth failed";
									delete is_pipe;
									is_pipe=NULL;
								}
							}
							else
							{
								errmsg="Unknown client";
							}
						}
						else
						{
							errmsg="Missing fields";
						}

						CWData data;
						if(!errmsg.empty())
						{
							Server->Log("Authentication failed in InternetServiceConnector::ReceivePackets: "+errmsg, LL_INFO);
							data.addChar(ID_ISC_AUTH_FAILED);
							data.addString(errmsg);
						}
						else
						{
							std::string client_challenge;
							rd.getStr(&client_challenge);
							data.addChar(ID_ISC_AUTH_OK);
							if(is_pipe!=NULL)
							{
								data.addString(is_pipe->encrypt(auth_text+client_challenge));
							}
						}
						tcpstack.Send(cs, data);
					}					
				}break;
			case ISS_CAPA:
				{
					if(id==ID_ISC_CAPA)
					{
						unsigned int capa;
						if( rd.getUInt(&capa) )
						{
							comm_pipe=cs;
							if(capa & IPC_ENCRYPTED )
							{
								is_pipe->setBackendPipe(comm_pipe);
								comm_pipe=is_pipe;
							}	
							if(capa & IPC_COMPRESSED )
							{
								comp_pipe=new CompressedPipe(comm_pipe, compression_level);
								comm_pipe=comp_pipe;
							}

							{
								IScopedLock lock(mutex);
								client_data[clientname].spare_connections.push(this);
								client_data[clientname].last_seen=Server->getTimeMS();
							}

							Server->Log("Authed+capa for client '"+clientname+"' - "+nconvert(client_data[clientname].spare_connections.size())+" spare connections", LL_DEBUG);

							state=ISS_AUTHED;
						}
					}
				}break;
			case ISS_AUTHED:
				{
					if(id==ID_ISC_PONG)
					{
						pinging=false;
						IScopedLock lock(mutex);
						client_data[clientname].last_seen=Server->getTimeMS();
					} 
				}break;
			case ISS_CONNECTING:
				{
					if(id==ID_ISC_CONNECT_OK)
					{
						state=ISS_USED;

						is_connected=true;

						IScopedLock lock(mutex);
						client_data[clientname].last_seen=Server->getTimeMS();
						
						connection_done_cond->notify_all();
						connection_done_cond=NULL;
					}
				}break;
			}
		}
		delete []buf;
	}
}

void InternetServiceConnector::init_mutex(void)
{
	mutex=Server->createMutex();
}

IPipe *InternetServiceConnector::getConnection(const std::string &clientname, char service, int timeoutms)
{

	unsigned int starttime=Server->getTimeMS();
	do
	{
		IScopedLock lock(mutex);
		std::map<std::string, SClientData>::iterator iter=client_data.find(clientname);
		if(iter==client_data.end())
			return NULL;

		if(iter->second.spare_connections.empty())
		{
			lock.relock(NULL);
			Server->wait(100);
		}
		else
		{
			InternetServiceConnector *isc=NULL;
			do{
				isc=iter->second.spare_connections.front();
				iter->second.spare_connections.pop();
				if(isc->hasTimeout())
				{
					isc->freeConnection();
					isc=NULL;
				}
			}
			while(isc==NULL && !iter->second.spare_connections.empty());

			if(isc==NULL)
				continue;

			ICondition *cond_ok=Server->createCondition();
			ICondition *cond_stop=Server->createCondition();
			if(isc->Connect(cond_ok, cond_stop, service))
			{
				unsigned int rtime=Server->getTimeMS()-starttime;
				if((int)rtime<timeoutms)
					rtime=timeoutms-rtime;
				else
					rtime=0;

				if(rtime<100) rtime=100;

				cond_ok->wait(&lock, rtime);
				
				if(!isc->isConnected())
				{
					isc->stopConnecting();
					cond_stop->wait(&lock);
					Server->destroy(cond_stop);
					Server->destroy(cond_ok);
					Server->Log("Connecting on internet connection failed. Service="+nconvert((int)service), LL_DEBUG);
				}
				else
				{
					Server->destroy(cond_stop);
					Server->destroy(cond_ok);
					IPipe *ret=isc->getISPipe();
					isc->freeConnection();
					CompressedPipe *comp_pipe=dynamic_cast<CompressedPipe*>(ret);
					if(comp_pipe!=NULL)
					{
						InternetServicePipe *isc_pipe=dynamic_cast<InternetServicePipe*>(comp_pipe->getRealPipe());
						if(isc_pipe!=NULL)
						{
							isc_pipe->destroyBackendPipeOnDelete(true);
						}
						comp_pipe->destroyBackendPipeOnDelete(true);
					}
					else
					{
						InternetServicePipe *isc_pipe=dynamic_cast<InternetServicePipe*>(ret);
						if(isc_pipe!=NULL)
						{
							isc_pipe->destroyBackendPipeOnDelete(true);
						}
					}
					Server->Log("Established internet connection. Service="+nconvert((int)service), LL_DEBUG);
					
					return ret;
				}
			}
			else
			{
				Server->Log("Error: Could not start connection process. Service="+nconvert((int)service), LL_ERROR);
			}
		}
	}while(timeoutms==-1 || Server->getTimeMS()-starttime<(unsigned int)timeoutms);

	Server->Log("Establishing internet connection failed. Service="+nconvert((int)service), LL_DEBUG);
	return NULL;
}

bool InternetServiceConnector::wantReceive(void)
{
	if(has_timeout)
		return false;

	if(state!=ISS_USED )
		return true;
	else
		return false;
}

bool InternetServiceConnector::closeSocket(void)
{
	if(has_timeout)
		return true;
	else if(stop_connecting)
		return true;
	else if(state!=ISS_USED)
		return true;
	else if(free_connection)
		return false;
	else
		return true;
}

bool InternetServiceConnector::Connect(ICondition *n_cond, ICondition *stop_cond, char service)
{
	IScopedLock lock(local_mutex);
	if(do_connect==false)
	{
		connection_done_cond=n_cond;
		connection_stop_cond=stop_cond;
		target_service=service;
		do_connect=true;
		return true;
	}
	return false;
}

IPipe *InternetServiceConnector::getISPipe(void)
{
	IScopedLock lock(local_mutex);
	return comm_pipe;
}

void InternetServiceConnector::stopConnecting(void)
{
	stop_connecting=true;
}

bool InternetServiceConnector::isConnected(void)
{
	return is_connected;
}

void InternetServiceConnector::freeConnection(void)
{
	free_connection=true;
}

std::vector<std::string> InternetServiceConnector::getOnlineClients(void)
{
	std::vector<std::string> ret;

	IScopedLock lock(mutex);
	unsigned int ct=Server->getTimeMS();
	std::vector<std::string> todel;
	for(std::map<std::string, SClientData>::iterator it=client_data.begin();it!=client_data.end();++it)
	{
		if(!it->second.spare_connections.empty())
		{
			if(ct-it->second.last_seen<offline_timeout)
			{
				ret.push_back(it->first);
			}
		}
		else
		{
			if(ct-it->second.last_seen>=establish_timeout)
			{
				todel.push_back(it->first);
			}
		}
	}

	for(size_t i=0;i<todel.size();++i)
	{
		std::map<std::string, SClientData>::iterator it=client_data.find(todel[i]);
		Server->Log("Establish timeout: Deleting internet client \""+it->first+"\"", LL_DEBUG);
		while(!it->second.spare_connections.empty())
		{
			InternetServiceConnector *isc=it->second.spare_connections.front();
			it->second.spare_connections.pop();
			isc->stopConnecting();
			isc->freeConnection();
		}
		client_data.erase(it);
	}

	return ret;
}

bool InternetServiceConnector::hasTimeout(void)
{
	return has_timeout;
}