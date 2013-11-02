/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "vld.h"

#include <deque>
#include <vector>

#include "SelectThread.h"
#include "Client.h"
#include "WorkerThread.h"
#include "Server.h"
#include "stringtools.h"
#include <errno.h>

std::vector<CWorkerThread*> workers;
IMutex* workers_mutex=NULL;
std::deque<CClient*> client_queue;
IMutex* clients_mutex=NULL;
ICondition* clients_cond=NULL;

void init_mutex_selthread(void)
{
    workers_mutex=Server->createMutex();
    clients_mutex=Server->createMutex();
    clients_cond=Server->createCondition();
}

void destroy_mutex_selthread(void)
{
    Server->destroy(workers_mutex);
    Server->destroy(clients_mutex);
    Server->destroy(clients_cond);
}

CSelectThread::CSelectThread(_u32 pWorkerThreadsPerMaster)
{
	mutex=Server->createMutex();
	stop_mutex=Server->createMutex();
	cond=Server->createCondition();
	stop_cond=Server->createCondition();
	
	IScopedLock lock(workers_mutex);
	if( workers.size()==0 )
	{
		for(size_t i=0;i<pWorkerThreadsPerMaster;++i)
		{
			CWorkerThread *wt=new CWorkerThread(this);
	
			workers.push_back( wt );
	
			Server->createThread(wt);
		}
	}
	run=true;
}

CSelectThread::~CSelectThread()
{
	Server->Log("waiting for selectthread...");
	{
		IScopedLock slock(stop_mutex);
		run=false;
		WakeUp();
		stop_cond->wait(&slock);
		Server->Log("deleting workers");
		IScopedLock lock(workers_mutex);
		for(size_t i=0;i<workers.size();++i)
		{
			Server->Log("worker: "+nconvert(i));
			delete workers[i];
		}
		workers.clear();
	}
	
	Server->destroy(mutex);
	Server->destroy(stop_mutex);
	Server->destroy(cond);
	Server->destroy(stop_cond);
}

void CSelectThread::operator()()
{
#ifdef _WIN32
	_i32 max;
	fd_set fdset;
#else
	std::vector<pollfd> conn;
#endif
	while(run)
	{
		{
			IScopedLock lock(mutex);
			while( clients.size()==0 )
			{
				cond->wait(&lock);
				if(!run)
				{
				  IScopedLock slock(stop_mutex);
				  stop_cond->notify_one();
				  return;
				}
			}

			bool np=true;
			while(np==true )
			{
				for(size_t i=0;i<clients.size();++i)
				{
					if( clients[i]->isProcessing()==false )
					{
						np=false;
					}
				}

				if( np==true )
				{
					cond->wait(&lock);
					if(!run)
					{
					  IScopedLock slock(stop_mutex);
					  stop_cond->notify_one();
					  return;
					}
				}
			}

			//Server->Log("SelectThread woke up...");
			
#ifdef _WIN32
			FD_ZERO(&fdset);
			max=0;
#endif

			for(size_t i=0;i<clients.size();++i)
			{
				if( clients[i]->isProcessing()==false )
				{
#ifdef _WIN32
					SOCKET s=clients[i]->getSocket();
					if((_i32)s>max)
						max=(_i32)s;
					FD_SET(s, &fdset);
#else
					pollfd nconn;
					nconn.fd=clients[i]->getSocket();
					nconn.events=POLLIN;
					nconn.revents=0;
					conn.push_back(nconn);
#endif
				}
			}
		}

#ifdef _WIN32
		timeval lon;
		lon.tv_sec=0;
		lon.tv_usec=10000;

		_i32 rc = select(max+1, &fdset, 0, 0, &lon);
#else
		int rc = poll(&conn[0], conn.size(), 10);
#endif

		if( rc>0)
		{
			IScopedLock lock(mutex);
#ifdef _WIN32
			for(size_t i=0;i<clients.size();++i)
			{
				if( clients[i]->isProcessing()==false )
				{
					SOCKET s=clients[i]->getSocket();
					if( FD_ISSET(s,&fdset) )
					{
						FindWorker(clients[i]);
					}
				}
			}
#else
			for(size_t i=0;i<conn.size();++i)
			{
				if(conn[i].revents!=0)
				{
					for(size_t j=0;j<clients.size();++j)
					{
						if(clients[j]->getSocket()==conn[i].fd)
						{
							FindWorker(clients[j]);
						}
					}
				}
			}
#endif
		}
		else if(rc==-1)
		{
			if( errno==EBADF )
			{
				Server->Log("Select error: EBADF",LL_ERROR);
			}
			else if( errno==EINTR )
			{
				Server->Log("Select error: EINTR", LL_ERROR);
			}
			else if( errno==ENOMEM )
			{
				Server->Log("Select error: ENOMEM", LL_ERROR);
			}
			else
			{
				Server->Log("Select error: "+nconvert(errno),LL_ERROR);
			}
		}
	}
	IScopedLock slock(stop_mutex);
	stop_cond->notify_one();
}

bool CSelectThread::AddClient(CClient *client)
{
	if( FreeClients()>0 )
	{
		IScopedLock lock(mutex);
		clients.push_back(client);
		WakeUp();
		return true;
	}
	return false;
}

size_t CSelectThread::FreeClients(void)
{
	IScopedLock lock(mutex);
	return max_clients-clients.size();
}

bool CSelectThread::RemoveClient(CClient *client)
{
	IScopedLock lock(mutex);
	for(size_t i=0;i<clients.size();++i)
	{
		if( clients[i]==client )
		{
			clients.erase( clients.begin()+i );
			client->remove();
			delete client;
			return true;
		}
	}
	return false;
}

void CSelectThread::FindWorker(CClient *client)
{
	//Server->Log("Notifying worker...");
	if( client->setProcessing(true) == false )
	{
		IScopedLock lock(clients_mutex);
		client_queue.push_back( client );
		clients_cond->notify_one();	
	}
}

void CSelectThread::WakeUp(void)
{
	cond->notify_one();
}
