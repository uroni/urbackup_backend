#include "InternetServiceConnector.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Database.h"
#include "../urbackupcommon/fileclient/data.h"
#include "../urbackupcommon/InternetServiceIDs.h"
#include "../urbackupcommon/InternetServicePipe.h"
#include "server_settings.h"
#include "database.h"

const unsigned int ping_interval=30*60*1000;
const unsigned int ping_timeout=30000;
const unsigned int offline_timeout=30000;

std::map<std::string, SClientData> InternetServiceConnector::client_data;
IMutex *InternetServiceConnector::mutex=NULL;

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
	is_pipe=NULL;
	do_connect=false;
	stop_connecting=false;
	is_connected=false;
	pinging=false;
	free_connection=false;
	state=ISS_AUTH;
	connection_done_cond=NULL;
	tcpstack.reset();
	challenge=ServerSettings::generateRandomAuthKey();
	{
		CWData data;
		data.addChar(ID_ISC_CHALLENGE);
		data.addString(challenge);
		tcpstack.Send(cs, data);
	}
	lastpingtime=Server->getTimeMS();
}

bool InternetServiceConnector::Run(void)
{
	if(state==ISS_CONNECTING || state==ISS_USED)
	{
		if(stop_connecting)
		{
			return false;
		}
		if(free_connection)
		{
			return false;
		}
		return true;
	}

	if(state==ISS_QUIT)
		return false;

	if(state==ISS_USED)
		return false;

	if(do_connect && state==ISS_AUTHED )
	{
		CWData data;
		data.addChar(ID_ISC_CONNECT);
		data.addChar(target_service);
		tcpstack.Send(is_pipe, data);
		state=ISS_CONNECTING;
		starttime=Server->getTimeMS();
	}

	if(state==ISS_AUTHED && Server->getTimeMS()-lastpingtime>ping_interval && !pinging)
	{
		lastpingtime=Server->getTimeMS();
		pinging=true;
		CWData data;
		data.addChar(ID_ISC_PING);
		tcpstack.Send(is_pipe, data);
	}
	else if(state==ISS_AUTHED && pinging )
	{
		if(Server->getTimeMS()-lastpingtime>ping_timeout)
		{
			Server->Log("Ping timeout in InternetServiceConnector::Run", LL_DEBUG);
			IScopedLock lock(local_mutex);
			state=ISS_QUIT;
			delete is_pipe;
			is_pipe=NULL;
			return false;
		}
	}
	return true;
}

void InternetServiceConnector::ReceivePackets(void)
{
	std::string ret;
	size_t rc;
	if(is_pipe==NULL)
		rc=cs->Read(&ret);
	else
		rc=is_pipe->Read(&ret);

	if(rc==0)
	{
		state=ISS_QUIT;
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
								is_pipe=new InternetServicePipe(cs, Server->ConvertToUTF8(res[0][L"value"]));
								
								std::string auth_str=is_pipe->decrypt(encdata);
								if(auth_str=="##################URBACKUP--AUTH#########################-"+challenge)
								{
									state=ISS_AUTHED;

									IScopedLock lock(mutex);
									client_data[clientname].spare_connections.push(this);
									client_data[clientname].last_seen=Server->getTimeMS();
								}
								else
								{
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
							data.addChar(ID_ISC_AUTH_OK);
						}
						tcpstack.Send(cs, data);
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
						connection_done_cond->notify_all();

						IScopedLock lock(mutex);
						client_data[clientname].last_seen=Server->getTimeMS();
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
			InternetServiceConnector *isc=iter->second.spare_connections.front();
			iter->second.spare_connections.pop();
			ICondition *cond_ok=Server->createCondition();
			if(isc->Connect(cond_ok, service))
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
					Server->Log("Connecting on internet connection failed", LL_DEBUG);
				}
				else
				{
					IPipe *ret=isc->getISPipe();
					isc->freeConnection();
					Server->Log("Established internet connection", LL_DEBUG);
					
					return ret;
				}
			}
			else
			{
				Server->Log("Error: Could not start connection process", LL_ERROR);
			}
		}
	}while(timeoutms==-1 || Server->getTimeMS()-starttime<(unsigned int)timeoutms);

	Server->Log("Establishing internet connection failed", LL_DEBUG);
	return NULL;
}

bool InternetServiceConnector::wantReceive(void)
{
	if(state!=ISS_USED)
		return true;
	else
		return false;
}

bool InternetServiceConnector::closeSocket(void)
{
	if(state!=ISS_USED)
		return true;
	else if(free_connection)
		return false;
	else
		return true;
}

bool InternetServiceConnector::Connect(ICondition *n_cond, char service)
{
	IScopedLock lock(local_mutex);
	if(do_connect==false)
	{
		connection_done_cond=n_cond;
		target_service=service;
		do_connect=true;
		return true;
	}
	return false;
}

IPipe *InternetServiceConnector::getISPipe(void)
{
	IScopedLock lock(local_mutex);
	return is_pipe;
}

bool InternetServiceConnector::stopConnecting(void)
{
	return stop_connecting;
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
			todel.push_back(it->first);
		}
	}

	for(size_t i=0;i<todel.size();++i)
	{
		std::map<std::string, SClientData>::iterator it=client_data.find(todel[i]);
		client_data.erase(it);
	}

	return ret;
}