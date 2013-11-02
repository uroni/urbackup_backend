#include "vld.h"
#include "Interface/Service.h"
#include "ServiceWorker.h"
#include "StreamPipe.h"
#include "Server.h"
#include "stringtools.h"
#include <stdlib.h>

CServiceWorker::CServiceWorker(IService *pService, std::string pName, IPipe * pExit, int pMaxClientsPerThread) : exit(pExit)
{
	mutex=Server->createMutex();
	nc_mutex=Server->createMutex();
	cond=Server->createCondition();
	
	name=pName;
	service=pService;
	nClients=0;
	do_stop=false;

	if(pMaxClientsPerThread>0)
	{
		max_clients=pMaxClientsPerThread;
	}
	else
	{
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
}

CServiceWorker::~CServiceWorker()
{
	for(size_t i=0;i<clients.size();++i)
	{
		service->destroyClient( clients[i].first );
		delete clients[i].second;
	}
	clients.clear();

	Server->destroy(mutex);
	Server->destroy(nc_mutex);
	Server->destroy(cond);
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

#ifdef _WIN32
	fd_set fdset;
	int max;
#else
	std::vector<pollfd> conn;
	std::vector<ICustomClient*> conn_clients;
#endif
	
	while(!do_stop)
	{
		{
			IScopedLock lock(mutex);
			addNewClients();
		}

		bool has_select_client=false;
		{
			{
				if( clients.empty() )
				{
					IScopedLock lock(mutex);
					if(new_clients.empty() && !do_stop)
					{
						//Server->Log(name+": Sleeping..."+nconvert(Server->getTimeMS()), LL_DEBUG);
						cond->wait(&lock);
						//Server->Log(name+": Waking up..."+nconvert(Server->getTimeMS()), LL_DEBUG);
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
					//Server->Log(name+": Removing user"+nconvert(Server->getTimeMS()), LL_DEBUG);
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

#ifdef _WIN32
			FD_ZERO(&fdset);
			max=0;
#else
			conn.clear();
			conn_clients.clear();
#endif

			for(size_t i=0;i<clients.size();++i)
			{
				if(clients[i].first->wantReceive())
				{
					SOCKET s=clients[i].second->getSocket();
#ifdef _WIN32
					if((_i32)s>max)
						max=(_i32)s;
					FD_SET(s, &fdset);
#else
					pollfd nconn;
					nconn.fd=s;
					nconn.events=POLLIN;
					nconn.revents=0;
					conn.push_back(nconn);
					conn_clients.push_back(clients[i].first);
#endif
					has_select_client=true;
				}
			}
		}


		if(has_select_client)
		{
#ifdef _WIN32
			timeval lon;
			lon.tv_sec=0;
			lon.tv_usec=10000;

			_i32 rc = select(max+1, &fdset, 0, 0, &lon);
#else
			int rc = poll(&conn[0], conn.size(), 10);
#endif
			if( rc>0 )
			{
#ifdef _WIN32
				for(size_t i=0;i<clients.size();++i)
				{
					SOCKET s=clients[i].second->getSocket();
					if( FD_ISSET(s,&fdset) )
					{
						//Server->Log("Incoming data for client..", LL_DEBUG);
						clients[i].first->ReceivePackets();					
					}
				}
#else
				for(size_t i=0;i<conn.size();++i)
				{
					if(conn[i].revents!=0)
					{
						conn_clients[i]->ReceivePackets();
					}
				}
#endif
			}		
		}
		else
		{
			Server->wait(10);
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
