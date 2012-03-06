#include "InternetServiceConnector.h"
#include "../Interface/Server.h"
#include "../Interface/Mutex.h"
#include "../Interface/Condition.h"
#include "../Interface/Database.h"
#include "../urbackupcommon/fileclient/data.h"
#include "../urbackupcommon/InternetServiceIDs.h"
#include "../urbackupcommon/InternetServicePipe.h"
#include "database.h"

const unsigned int ping_interval=30*60*1000;
const unsigned int ping_timeout=30000;

std::queue<InternetServiceConnector*> InternetServiceConnector::spare_connections;
IMutex *InternetServiceConnector::mutex=NULL;

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
}

bool InternetServiceConnector::Run(void)
{
	if(state==ISS_CONNECTING || state==ISS_USED)
	{
		if(stop_connecting)
		{
			connection_quit_cond->notify_all();
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
			state=ISS_QUIT;
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
						std::string clientname;
						std::string encdata;
						std::string errmsg;

						if(rd.getStr(&clientname) && rd.getStr(&encdata) )
						{
							IDatabase *db=Server->getDatabase(tid, URBACKUPDB_SERVER);
							IQuery *q=db->Prepare("SELECT key FROM clients WHERE name=?", false);
							q->Bind(clientname);
							db_results res=q->Read();
							db->destroyQuery(q);
							if(!res.empty())
							{
								is_pipe=new InternetServicePipe(cs, Server->ConvertToUTF8(res[0][L"key"]));
								
								std::string auth_str=is_pipe->decrypt(encdata);
								if(auth_str=="##################URBACKUP--AUTH#########################")
								{
									state=ISS_AUTHED;

									IScopedLock lock(mutex);
									spare_connections.push(this);
								}
								else
								{
									errmsg="Auth failed";
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
					}
				}break;
			case ISS_CONNECTING:
				{
					if(id==ID_ISC_CONNECT_OK)
					{
						state=ISS_USED;

						is_connected=true;
						connection_done_cond->notify_all();
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

IPipe *InternetServiceConnector::getConnection(char service, int timeoutms)
{
	unsigned int starttime=Server->getTimeMS();
	do
	{
		IScopedLock lock(mutex);
		if(spare_connections.empty())
		{
			lock.relock(NULL);
			Server->wait(100);
		}
		else
		{
			InternetServiceConnector *isc=spare_connections.front();
			ICondition *cond_ok=Server->createCondition();
			ICondition *cond_quit=Server->createCondition();
			if(isc->Connect(cond_ok, cond_quit, service))
			{
				unsigned int rtime=Server->getTimeMS()-starttime;
				if(rtime<timeoutms)
					rtime=timeoutms-rtime;
				else
					rtime=0;

				if(rtime<100) rtime=100;

				cond_ok->wait(&lock, rtime);
				
				if(!isc->isConnected())
				{
					isc->stopConnecting();
					cond_quit->wait(&lock);
				}
				else
				{
					isc->freeConnection();
					return isc->getISPipe();
				}
			}
			else
			{
				Server->Log("Error: Could not start connection process", LL_ERROR);
			}
		}
	}while(timeoutms==-1 || Server->getTimeMS()-starttime<timeoutms);
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

bool InternetServiceConnector::Connect(ICondition *n_cond, ICondition *q_cond, char service)
{
	IScopedLock lock(local_mutex);
	if(do_connect==false)
	{
		connection_done_cond=n_cond;
		connection_quit_cond=q_cond;
		target_service=service;
		do_connect=true;
		return true;
	}
	return false;
}

IPipe *InternetServiceConnector::getISPipe(void)
{
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