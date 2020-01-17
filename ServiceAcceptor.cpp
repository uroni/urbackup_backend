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

namespace
{
	bool prepareSocket(SOCKET s)
	{
#if !defined(_WIN32) && !defined(SOCK_CLOEXEC)
		fcntl(s, F_SETFD, fcntl(s, F_GETFD, 0) | FD_CLOEXEC);
#endif

		int optval = 1;
		int rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
		if (rc == SOCKET_ERROR)
		{
			Server->Log("Failed setting SO_REUSEADDR", LL_ERROR);
			return false;
		}

#ifdef __APPLE__
		optval = 1;
		setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void*)&optval, sizeof(optval));
#endif
		return true;
	}
}

CServiceAcceptor::CServiceAcceptor(IService * pService, std::string pName, unsigned short port, int pMaxClientsPerThread, IServer::BindTarget bindTarget)
	: maxClientsPerThread(pMaxClientsPerThread), s(SOCKET_ERROR), s_v6(SOCKET_ERROR)
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
	rc = WSAStartup(MAKEWORD(2,2), &wsadata);
	if(rc == SOCKET_ERROR)	return;
#endif

	bool has_ipv4 = Server->getServerParameter("disable_ipv4").empty();
	bool has_ipv6 = Server->getServerParameter("disable_ipv6").empty();
	
	if (!has_ipv4 && !has_ipv6)
	{
		Server->Log(name + ": IPv4 and IPv6 disabled", LL_ERROR);
		has_error = true;
		return;
	}

	if (has_ipv4)
	{
		if (!init_socket_v4(port, bindTarget))
		{
			if (Server->getServerParameter("ipv4_optional").empty()
				|| !has_ipv6)
			{
				has_error = true;
				return;
			}
		}
	}

	if (has_ipv6)
	{
		if (!init_socket_v6(port, bindTarget))
		{
			if (!Server->getServerParameter("ipv6_required").empty()
				|| !has_ipv4)
			{
				has_error = true;
				return;
			}
		}
	}

	Server->Log(name+": Server started up successfully!",LL_INFO);
}

CServiceAcceptor::~CServiceAcceptor()
{
	do_exit = true;

#ifndef _WIN32
	char ch = 0;
	write(xpipe[1], &ch, 1);
#endif

	if (s != SOCKET_ERROR)
		closesocket(s);
	if (s_v6 != SOCKET_ERROR)
		closesocket(s_v6);

	if(has_error)
		return;
	
	for(size_t i=0;i<workers.size();++i)
	{
		workers[i]->stop();
	}
	size_t c=0;
	Server->Log("c=0/"+convert(workers.size()+1));
	while(c<workers.size()+1)
	{
		std::string r;
		exitpipe->Read(&r);
		if(r=="ok")
		{
			++c;
			Server->Log("c="+convert(c)+"/"+convert(workers.size()+1));
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

	while(!do_exit)
	{
#ifdef _WIN32
		fd_set fdset;
		FD_ZERO(&fdset);		
		if(s!=SOCKET_ERROR)
			FD_SET(s, &fdset);
		if (s_v6 != SOCKET_ERROR)
			FD_SET(s_v6, &fdset);
		SOCKET maxs = (std::max)(s, s_v6) + 1;

		timeval lon;
		lon.tv_sec=100;
		lon.tv_usec=0;

		_i32 rc=select((int)maxs, &fdset, 0, 0, &lon);
		size_t num_fds = 2;
#else
		pollfd conn[3];
		conn[0].fd= s !=SOCKET_ERROR ? s: s_v6;
		conn[0].events=POLLIN;
		conn[0].revents=0;
		conn[1].fd=xpipe[0];
		conn[1].events=POLLIN;
		conn[1].revents=0;
		conn[2].revents = 0;

		nfds_t num_fds = 2;

		if (s_v6 != SOCKET_ERROR
			&& s != SOCKET_ERROR)
		{
			num_fds = 3;
			conn[2].fd = s_v6;
			conn[2].events = POLLIN;
		}

		int rc = poll(conn, num_fds, 100*1000);
#endif
		if(rc>=0)
		{
			for (size_t n = 0; n < num_fds; ++n)
			{
#ifdef _WIN32
				SOCKET accept_socket = n == 0 ? s : s_v6;
				if (accept_socket == SOCKET_ERROR)
					continue;
				if (!FD_ISSET(accept_socket, &fdset))
					continue;
#else
				if (n == 1)
					continue;
				if (conn[n].revents == 0)
					continue;
				SOCKET accept_socket = conn[n].fd;
#endif
				if (do_exit)
					break;

				union
				{
					sockaddr_in naddr_v4;
					sockaddr_in6 naddr_v6;
				} naddr;
				socklen_t addrsize;
				if (accept_socket == s_v6)
					addrsize = sizeof(naddr.naddr_v6);
				else
					addrsize = sizeof(naddr.naddr_v4);

				sockaddr* paddr = (sockaddr*)&naddr;
				SOCKET ns= ACCEPT_CLOEXEC(accept_socket, paddr, &addrsize);
				if(ns!=SOCKET_ERROR)
				{
#ifdef __APPLE__
					int optval = 1;
					setsockopt(ns, SOL_SOCKET, SO_NOSIGPIPE, (void*)&optval, sizeof(optval));
#endif
					//Server->Log(name+": New Connection incomming "+convert(Server->getTimeMS())+" s: "+convert((int)ns), LL_DEBUG);

	#ifdef _WIN32
					int window_size=Server->getSendWindowSize();
					if(window_size>0)
						setsockopt(ns, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size));

					window_size = Server->getRecvWindowSize();
					if(window_size>0)
						setsockopt(ns, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size));
	#endif
					char buf[100];
					const char* endpoint = inet_ntop(paddr->sa_family,
						accept_socket == s_v6 ? (void*)&naddr.naddr_v6.sin6_addr : (void*)&naddr.naddr_v4.sin_addr,
						buf, sizeof(buf));

					AddToWorker(ns, endpoint!=NULL ? endpoint : "");
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

	Server->createThread(nw, name+": worker");

	nw->AddClient( pSocket, endpoint );
}

bool CServiceAcceptor::init_socket_v4(unsigned short port, IServer::BindTarget bindTarget)
{
	int type = SOCK_STREAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	s = socket(AF_INET, type, 0);
	if (s == SOCKET_ERROR)
	{
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL)
		{
			type &= ~SOCK_CLOEXEC;
			s = socket(AF_INET, type, 0);
		}
#endif
		if (s == SOCKET_ERROR)
		{
			Server->Log(name + ": Creating SOCKET failed", LL_ERROR);
			return false;
		}
	}

	if (!prepareSocket(s))
	{
		closesocket(s);
		s = SOCKET_ERROR;
		return false;
	}

	sockaddr_in addr;

	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	switch (bindTarget)
	{
	case IServer::BindTarget_All:
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		break;
	case IServer::BindTarget_Localhost:
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		break;
	}

	int rc = bind(s, (sockaddr*)& addr, sizeof(addr));
	if (rc == SOCKET_ERROR)
	{
		Server->Log(name + ": Failed binding socket to port " + convert(port) + ". Another instance of this application may already be active and bound to this port.", LL_ERROR);
		closesocket(s);
		s = SOCKET_ERROR;
		return false;
	}

	listen(s, 10000);

	return true;
}

bool CServiceAcceptor::init_socket_v6(unsigned short port, IServer::BindTarget bindTarget)
{
	int type = SOCK_STREAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif

	s_v6 = socket(AF_INET6, type, 0);
	if (s_v6 < 1)
	{
		Server->Log(name + ": Creating v6 SOCKET failed", LL_ERROR);
		return false;
	}

	if (!prepareSocket(s_v6))
	{
		closesocket(s_v6);
		s_v6 = SOCKET_ERROR;
		return false;
	}

	int optval = 1;
	setsockopt(s_v6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)& optval, sizeof(optval));

	sockaddr_in6 addr;

	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	switch (bindTarget)
	{
	case IServer::BindTarget_All:
		addr.sin6_addr = in6addr_any;
		break;
	case IServer::BindTarget_Localhost:
		addr.sin6_addr = in6addr_loopback;
		break;
	}

	int rc = bind(s_v6, (sockaddr*)& addr, sizeof(addr));
	if (rc == SOCKET_ERROR)
	{
		Server->Log(name + ": Failed binding ipv6 socket to port " + convert(port) + ". Another instance of this application may already be active and bound to this port.", LL_ERROR);
		closesocket(s_v6);
		s_v6 = SOCKET_ERROR;
		return false;
	}

	listen(s_v6, 10000);

	return true;
}

