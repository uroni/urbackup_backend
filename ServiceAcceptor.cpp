/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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
#ifdef _WIN32
#include <winsock2.h>
#endif
#include "ServiceAcceptor.h"
#include "stringtools.h"
#include "ServiceWorker.h"
#include <memory.h>

#include "Interface/Mutex.h"
#include "Interface/Condition.h"
#include "Interface/Service.h"

CServiceAcceptor::CServiceAcceptor(IService * pService, std::string pName, unsigned short port, int pMaxClientsPerThread, IServer::BindTarget bindTarget)
	: maxClientsPerThread(pMaxClientsPerThread)
{
	name=pName;
	service=pService;
	exitpipe=Server->createMemoryPipe();
	do_exit=false;
	has_error=false;
#ifndef _WIN32
	pipe(xpipe);
#endif

	int rc;
#ifdef _WIN32
	WSADATA wsadata;
	rc = WSAStartup(MAKEWORD(2,0), &wsadata);
	if(rc == SOCKET_ERROR)	return;
#endif

	s=socket(AF_INET,SOCK_STREAM,0);
	if(s<1)
	{
		Server->Log(name+": Creating SOCKET failed",LL_ERROR);
		has_error=true;
		return;
	}

	int optval=1;
	rc=setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
	if(rc==SOCKET_ERROR)
	{
		Server->Log("Failed setting SO_REUSEADDR for port "+nconvert(port),LL_ERROR);
		has_error=true;
		return;
	}

	sockaddr_in addr;

	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(port);
	switch(bindTarget)
	{
	case IServer::BindTarget_All:
		addr.sin_addr.s_addr=INADDR_ANY;
		break;
	case IServer::BindTarget_Localhost:
		addr.sin_addr.s_addr=INADDR_LOOPBACK;
		break;
	}

	rc=bind(s,(sockaddr*)&addr,sizeof(addr));
	if(rc==SOCKET_ERROR)
	{
		Server->Log(name+": Failed binding SOCKET to Port "+nconvert(port),LL_ERROR);
		has_error=true;
		return;
	}

	listen(s, 10000);

	Server->Log(name+": Server started up sucessfully!",LL_INFO);
}

CServiceAcceptor::~CServiceAcceptor()
{
	if(has_error)
		return;

	do_exit=true;
#ifndef _WIN32
	char ch=0;
	write(xpipe[1], &ch, 1);
#endif
	closesocket(s);
	for(size_t i=0;i<workers.size();++i)
	{
		workers[i]->stop();
	}
	size_t c=0;
	Server->Log("c=0/"+nconvert(workers.size()+1));
	while(c<workers.size()+1)
	{
		std::string r;
		exitpipe->Read(&r);
		if(r=="ok")
		{
			++c;
			Server->Log("c="+nconvert(c)+"/"+nconvert(workers.size()+1));
		}
	}
	Server->destroy(exitpipe);
	for(size_t i=0;i<workers.size();++i)
	{
		delete workers[i];
	}

	delete service;
}

void CServiceAcceptor::operator()(void)
{
	if(has_error)
		return;

	while(do_exit==false)
	{
		socklen_t addrsize=sizeof(sockaddr_in);

#ifdef _WIN32
		fd_set fdset;
		FD_ZERO(&fdset);		
		SOCKET maxs=s;
		FD_SET(s, &fdset);
		++maxs;

		timeval lon;
		lon.tv_sec=100;
		lon.tv_usec=0;

		_i32 rc=select((int)maxs, &fdset, 0, 0, &lon);
#else
		pollfd conn[2];
		conn[0].fd=s;
		conn[0].events=POLLIN;
		conn[0].revents=0;
		conn[1].fd=xpipe[0];
		conn[1].events=POLLIN;
		conn[1].revents=0;
		int rc = poll(conn, 2, 100*1000);
#endif
		if(rc>=0)
		{
#ifdef _WIN32
			if( FD_ISSET(s,&fdset) && do_exit==false)
#else
			if( conn[0].revents!=0 && do_exit==false)
#endif
			{
				sockaddr_in naddr;
				SOCKET ns=accept(s, (sockaddr*)&naddr, &addrsize);
				if(ns!=SOCKET_ERROR)
				{
					//Server->Log(name+": New Connection incomming "+nconvert(Server->getTimeMS())+" s: "+nconvert((int)ns), LL_DEBUG);

	#ifdef _WIN32
					int window_size=512*1024;
					setsockopt(ns, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size));
					setsockopt(ns, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size));
	#endif
					std::string endpoint = inet_ntoa(naddr.sin_addr);

					AddToWorker(ns, endpoint);				
				}
			}
		}
		else
		{
			Server->Log("Select error in CServiceAcceptor Thread.", LL_ERROR);
			break;
		}
	}
	Server->Log("ServiceAcceptor finished", LL_DEBUG);
	exitpipe->Write("ok");
}

void CServiceAcceptor::AddToWorker(SOCKET pSocket, const std::string& endpoint)
{
	for(size_t i=0;i<workers.size();++i)
	{
		if( workers[i]->getAvailableSlots()>0 )
		{
			workers[i]->AddClient(pSocket, endpoint);
			return;
		}
	}

	Server->Log(name+": No available slots... starting new Worker", LL_DEBUG);

	CServiceWorker *nw=new CServiceWorker(service, name, exitpipe, maxClientsPerThread);
	workers.push_back(nw);

	Server->createThread(nw);

	nw->AddClient( pSocket, endpoint );
}	
