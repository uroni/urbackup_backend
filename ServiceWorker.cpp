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

#include "vld.h"
#include "Interface/Service.h"
#include "ServiceWorker.h"
#include "StreamPipe.h"
#include "Server.h"
#include "stringtools.h"
#include <stdlib.h>

#ifndef _WIN32
#include "common/clopipe.h"
#endif

const int MAX_WORKER_WAIT_MS = 60000;
const int MIN_WORKER_WAIT_MS = 10;

CServiceWorker::CServiceWorker(IService *pService, std::string pName, IPipe * pExit, int pMaxClientsPerThread)
	: exit(pExit), tid(0)
{
	mutex=Server->createMutex();
	nc_mutex=Server->createMutex();
	
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

#ifdef _WIN32
	wakeup_event = WSACreateEvent();
	if (wakeup_event == WSA_INVALID_EVENT)
	{
		Server->Log("Error creating WSA wakeup event: " + convert(WSAGetLastError()));
		throw std::runtime_error("Error creating WSA wakeup event");
	}

	max_clients = min(WSA_MAXIMUM_WAIT_EVENTS-1, max_clients);
#else
	if (clopipe(wakeup_event) == -1)
	{
		Server->Log("Error creating wakeup pipe: " + convert(errno), LL_ERROR);
		throw std::runtime_error("Error creating wakeup pipe");
	}
#endif
}

CServiceWorker::~CServiceWorker()
{
	for(size_t i=0;i<clients.size();++i)
	{
		service->destroyClient( clients[i].first );
		delete clients[i].second;

#ifdef _WIN32
		WSACloseEvent(client_events[i]);
#endif
	}
	clients.clear();

	Server->destroy(mutex);
	Server->destroy(nc_mutex);

#ifdef _WIN32
	WSACloseEvent(wakeup_event);
#else
	close(wakeup_event[0]);
	close(wakeup_event[1]);
#endif
}

void CServiceWorker::stop(void)
{
	IScopedLock lock(mutex);
	do_stop=true;
	wakeupInt();
}

void CServiceWorker::work()
{
	int curr_wtime = MAX_WORKER_WAIT_MS;

	for (size_t i = 0; i<clients.size();)
	{
		const int wtime = clients[i].first->Run();

		if (wtime < 0)
		{
			if (clients[i].first->closeSocket())
			{
				delete clients[i].second;
			}
			service->destroyClient(clients[i].first);
			IScopedLock lock(mutex);
			clients.erase(clients.begin() + i);
			IScopedLock lock2(nc_mutex);
			--nClients;
		}
		else
		{
			if (wtime == 0)
				curr_wtime = MIN_WORKER_WAIT_MS;
			else if (wtime < curr_wtime)
				curr_wtime = wtime;

			++i;
		}
	}

#ifdef _WIN32
	WSAEVENT eventArray[WSA_MAXIMUM_WAIT_EVENTS];
	DWORD numEvents = 1;
	eventArray[0] = wakeup_event;
	std::vector<std::pair<ICustomClient*, SOCKET>> conn_clients;
#else
	std::vector<ICustomClient*> conn_clients;
	std::vector<pollfd> conn;
	pollfd nconn;
	nconn.fd = wakeup_event[0];
	nconn.events = POLLIN;
	nconn.revents = 0;
	conn.push_back(nconn);
#endif


	for (size_t i = 0; i<clients.size(); ++i)
	{
		if (clients[i].first->wantReceive())
		{
			SOCKET s = clients[i].second->getSocket();
#ifdef _WIN32
			eventArray[numEvents] = client_events[i];
			++numEvents;
			conn_clients.push_back(std::make_pair(clients[i].first, s));
#else
			pollfd nconn;
			nconn.fd = s;
			nconn.events = POLLIN;
			nconn.revents = 0;
			conn.push_back(nconn);
			conn_clients.push_back(clients[i].first);
#endif			
		}
	}


#ifdef _WIN32
	const DWORD rc = WSAWaitForMultipleEvents(numEvents, eventArray, FALSE, curr_wtime, FALSE);

	if (rc == WSA_WAIT_FAILED)
	{
		Server->Log("Error waiting for network events: " + convert(WSAGetLastError()), LL_ERROR);
		Server->wait(100);
	}
	else if (rc != WSA_WAIT_TIMEOUT)
	{
		if (rc == WSA_WAIT_EVENT_0)
		{
			ResetEvent(wakeup_event);
		}

		WSANETWORKEVENTS networkEvents;
		for (DWORD i = 1; i< numEvents;++i)
		{
			const SOCKET s = conn_clients[i-1].second;
			if (WSAEnumNetworkEvents(s, eventArray[i], &networkEvents) == 0
				&& networkEvents.lNetworkEvents>0)
			{
				conn_clients[i - 1].first->ReceivePackets();
			}
		}
	}

#else
	const int rc = poll(&conn[0], conn.size(), curr_wtime);

	if (rc > 0)
	{
		if (conn[0].revents != 0)
		{
			char ch;
			ssize_t r = read(wakeup_event[0], &ch, 1);
			if (r != 0)
			{
				Server->Log("Error reading from pipe fd", LL_WARNING);
			}
		}

		for (size_t i = 1; i < conn.size(); ++i)
		{
			if (conn[i].revents != 0)
			{
				conn_clients[i-1]->ReceivePackets();
			}
		}
	}
#endif
}


void CServiceWorker::addNewClients(void)
{
    for(size_t i=0;i<new_clients.size();++i)
    {
		CStreamPipe *pipe=new CStreamPipe(new_clients[i].first, "ServiceWorker " + name);
		ICustomClient *nc=service->createClient();
		nc->Init(tid, pipe, new_clients[i].second, this);
		clients.push_back( std::pair<ICustomClient*, CStreamPipe*>(nc, pipe) );

#ifdef _WIN32
		const WSAEVENT socket_event = WSACreateEvent();
		if (socket_event == WSA_INVALID_EVENT)
		{
			Server->Log("Error creating WSA socket event: " + convert(WSAGetLastError()), LL_ERROR);
			throw std::runtime_error("Error creating WSA socket event");
		}

		client_events.push_back(socket_event);

		const int rc = WSAEventSelect(pipe->getSocket(), socket_event, FD_READ | FD_CLOSE);
		if (rc == SOCKET_ERROR)
		{
			Server->Log("Error setting event select: " + convert(WSAGetLastError()), LL_ERROR);
			throw std::runtime_error("Error setting event select");
		}
#endif

    }
    new_clients.clear();
}

void CServiceWorker::operator()(void)
{
	tid=Server->getThreadID();
	
	while(true)
	{
		{
			IScopedLock lock(mutex);
			if (do_stop)
				break;
			addNewClients();
		}

		work();
	}
	Server->Log("ServiceWorker finished", LL_DEBUG);
	exit->Write("ok");
}

int CServiceWorker::getAvailableSlots(void)
{
	IScopedLock lock(nc_mutex);
	return max_clients-nClients;
}

void CServiceWorker::AddClient(SOCKET pSocket, const std::string& endpoint)
{
	IScopedLock lock(mutex);

	new_clients.push_back( std::make_pair(pSocket, endpoint) );

	wakeupInt();

	IScopedLock lock2(nc_mutex);
	++nClients;
}

void CServiceWorker::wakeupInt() const
{
#ifdef _WIN32
	const BOOL b = WSASetEvent(wakeup_event);
	if (!b)
	{
		Server->Log("Error setting wakeup_event: " + convert(WSAGetLastError()), LL_ERROR);
	}
#else
	char ch = 1;
	ssize_t rc = write(wakeup_event[1], &ch, 1);
	if (rc != 1)
	{
		Server->Log("Error writing to wakeup fd: " + convert(errno), LL_ERROR);
	}
#endif
}

void CServiceWorker::wakeup() const
{
	IScopedLock lock(mutex);
	wakeupInt();
}