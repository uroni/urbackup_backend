#include "vld.h"
#include "Interface/Service.h"
#include "ServiceWorker.h"
#include "StreamPipe.h"
#include "Server.h"
#include "stringtools.h"
#include <stdlib.h>

CServiceWorker::CServiceWorker(IService *pService, std::string pName, IPipe * pExit) : exit(pExit)
{
	mutex=Server->createMutex();
	nc_mutex=Server->createMutex();
	cond=Server->createCondition();
	
	name=pName;
	service=pService;
	nClients=0;
	do_stop=false;

	std::string s_max_clients;
	if((s_max_clients=Server->getServerParameter("max_worker_clients"))!="")
	{
		max_clients=atoi(s_max_clients.c_str());
	}
	else
	{
		max_clients=MAX_CLIENTS;
	}
}

CServiceWorker::~CServiceWorker()
{
	for(size_t i=0;i<clients.size();++i)
	{
		service->destroyClient( clients[i].first );
		delete clients[i].second;
	}
	clients.clear();
}

void CServiceWorker::stop(void)
{
	IScopedLock lock(mutex);
	do_stop=true;
	cond->notify_all();
}

void CServiceWorker::addNewClients(void)
{
    for(size_t i=0;i<new_clients.size();++i)
    {
		CStreamPipe *pipe=new CStreamPipe(new_clients[i]);
		ICustomClient *nc=service->createClient();
		nc->Init(tid, pipe);
		clients.push_back( std::pair<ICustomClient*, CStreamPipe*>(nc, pipe) );
    }
    new_clients.clear();
}    

void CServiceWorker::operator()(void)
{
	tid=Server->getThreadID();

	fd_set fdset;
	int max;
	while(!do_stop)
	{
		{
			IScopedLock lock(mutex);
			addNewClients();
		}
		{
			{
				if( clients.empty() )
				{
					IScopedLock lock(mutex);
					if(new_clients.empty() && !do_stop)
					{
						Server->Log(name+": Sleeping..."+nconvert(Server->getTimeMS()), LL_DEBUG);
						cond->wait(&lock);
						Server->Log(name+": Waking up..."+nconvert(Server->getTimeMS()), LL_DEBUG);
						continue;
					}
					else
					{
						continue;
					}
				}
			}

			for(size_t i=0;i<clients.size();++i)
			{
				bool b=clients[i].first->Run();

				if( b==false )
				{
					IScopedLock lock(mutex);
					Server->Log(name+": Removing user"+nconvert(Server->getTimeMS()), LL_DEBUG);
					if(clients[i].first->closeSocket())
					{
						delete clients[i].second;
					}
					service->destroyClient( clients[i].first );					
					clients.erase( clients.begin()+i );
					IScopedLock lock2(nc_mutex);
					--nClients;
					continue;
				}
			}

			FD_ZERO(&fdset);
			max=0;

			for(size_t i=0;i<clients.size();++i)
			{
				if(clients[i].first->wantReceive())
				{
					SOCKET s=clients[i].second->getSocket();
					if((_i32)s>max)
						max=(_i32)s;
					FD_SET(s, &fdset);
				}
			}
		}

		timeval lon;
		lon.tv_sec=0;
		lon.tv_usec=10000;

		_i32 rc = select(max+1, &fdset, 0, 0, &lon);

		if( rc>0 )
		{
			for(size_t i=0;i<clients.size();++i)
			{
				SOCKET s=clients[i].second->getSocket();
				if( FD_ISSET(s,&fdset) )
				{
					Server->Log("Incoming data for client..", LL_DEBUG);
					clients[i].first->ReceivePackets();					
				}
			}
		}				 
	}
	Server->Log("ServiceWorker finished", LL_DEBUG);
	exit->Write("ok");
}

int CServiceWorker::getAvailableSlots(void)
{
	IScopedLock lock(nc_mutex);
	return max_clients-nClients;
}

void CServiceWorker::AddClient(SOCKET pSocket)
{
	IScopedLock lock(mutex);

	new_clients.push_back( pSocket );
	
	cond->notify_all();
	
	IScopedLock lock2(nc_mutex);
	++nClients;
}
