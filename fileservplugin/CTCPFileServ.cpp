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

#include "../vld.h"
#include "../Interface/Server.h"
#include "CClientThread.h"
#include "CTCPFileServ.h"
#include "CUDPThread.h"
#include "../stringtools.h"
#include "packet_ids.h"
#include "map_buffer.h"
#include "../socket_header.h"
#include "log.h"
#include <memory.h>

#include <iostream>

#define REFRESH_SECONDS 10

CTCPFileServ::CTCPFileServ(void)
	: mSocket(SOCKET_ERROR), mSocketv6(SOCKET_ERROR)
{
	udpthread=nullptr;
	udpticket=ILLEGAL_THREADPOOL_TICKET;
	m_use_fqdn=false;
}

CTCPFileServ::~CTCPFileServ(void)
{
	if(udpthread!=nullptr)
	{
		udpthread->stop();
		if(udpticket!=ILLEGAL_THREADPOOL_TICKET)
		{
			Server->getThreadPool()->waitFor(udpticket);
		}
	}
}

void CTCPFileServ::KickClients()
{
	if (mSocket != SOCKET_ERROR)
	{
		closesocket(mSocket);
		mSocket = SOCKET_ERROR;
	}

	if (mSocketv6 != SOCKET_ERROR)
	{
		closesocket(mSocketv6);
		mSocketv6 = SOCKET_ERROR;
	}

	cs.Enter();
	for(size_t i=0;i<clientthreads.size();++i)
	{
		clientthreads[i]->StopThread();
	}
	while(!clientthreads.empty())
	{
		bool killed=true;
		while(killed==true )
		{
			killed=false;
			for(size_t i=0;i<clientthreads.size();++i)
			{
				clientthreads[i]->StopThread();
				if( clientthreads[i]->isKillable() )
				{
					killed=true;
					delete clientthreads[i];
					clientthreads.erase( clientthreads.begin()+i );
					break;
				}
			}
		}
		cs.Leave();
		Sleep(100);
		cs.Enter();
	}
	cs.Leave();
}

_u16 CTCPFileServ::getUDPPort()
{
	return m_udpport;
}

_u16 CTCPFileServ::getTCPPort()
{
	return m_tcpport;
}

std::string CTCPFileServ::getServername()
{
	return udpthread->getServername();
}

namespace
{
	bool setSocketSettings(SOCKET mSocket)
	{
#if !defined(_WIN32) && !defined(SOCK_CLOEXEC)
		fcntl(mSocket, F_SETFD, fcntl(mSocket, F_GETFD, 0) | FD_CLOEXEC);
#endif

#ifndef DISABLE_WINDOW_SIZE
		//Set window size
		int window_size = Server->getSendWindowSize();
		if (window_size > 0)
		{
			int err = setsockopt(mSocket, SOL_SOCKET, SO_SNDBUF, (char *)&window_size, sizeof(window_size));

			if (err == SOCKET_ERROR)
				Log("Error: Can't modify SO_SNDBUF", LL_DEBUG);
		}

		window_size = Server->getRecvWindowSize();
		if (window_size > 0)
		{
			int err = setsockopt(mSocket, SOL_SOCKET, SO_RCVBUF, (char *)&window_size, sizeof(window_size));

			if (err == SOCKET_ERROR)
				Log("Error: Can't modify SO_RCVBUF", LL_DEBUG);
		}
#endif
		int optval = 1;
		int rc = setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
		if (rc == SOCKET_ERROR)
		{
			Log("Failed setting SO_REUSEADDR in CTCPFileServ::Start", LL_ERROR);
			return false;
		}
#ifdef __APPLE__
		optval = 1;
		setsockopt(mSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&optval, sizeof(optval));
#endif

		return true;
	}
}

bool CTCPFileServ::Start(_u16 tcpport,_u16 udpport, std::string pServername, bool use_fqdn)
{
	m_tcpport=tcpport;
	m_udpport=udpport;
	m_use_fqdn=use_fqdn;
	_i32 rc;
#ifdef _WIN32
	WSADATA wsadata;
	rc = WSAStartup(MAKEWORD(2,2), &wsadata);
	if(rc == SOCKET_ERROR)	return false;
#endif

	if(tcpport!=0)
	{
		bool has_ipv4 = Server->getServerParameter("disable_ipv4").empty();
		bool has_ipv6 = Server->getServerParameter("disable_ipv6").empty();

		if (!has_ipv4 && !has_ipv6)
		{
			Log("IPv4 and IPv6 disabled", LL_ERROR);
			return false;
		}

		if (has_ipv4
			&& !startIpv4(tcpport) )
		{
			if (Server->getServerParameter("ipv4_optional").empty()
				|| !has_ipv6)
			{
				return false;
			}
		}

		if (has_ipv6
			&& !startIpv6(tcpport) )
		{
			if (!Server->getServerParameter("ipv6_required").empty()
				|| !has_ipv4)
			{
				return false;
			}
		}
	}
	//start udpsock
	if(udpthread!=nullptr && udpthread->hasError() )
	{
		delete udpthread;
		udpthread=nullptr;
	}
	if(udpthread==nullptr && udpport!=0)
	{
		udpthread=new CUDPThread(udpport,pServername, use_fqdn);
		if(!udpthread->hasError())
		{
			udpticket=Server->getThreadPool()->execute(udpthread, "filesrv: broadcast response");
		}
		else
		{
			delete udpthread;
			udpthread=nullptr;
			Log("Error starting UDP thread", LL_ERROR);
			return false;
		}
	}

	Log("Server started up successfully", LL_DEBUG);

    return true;
}

bool CTCPFileServ::Run(void)
{
	TcpStep();
	DelClientThreads();
	return true;
}

bool CTCPFileServ::TcpStep(void)
{
	if (m_tcpport == 0)
	{
		Server->wait(REFRESH_SECONDS * 1000);
		return true;
	}

#ifdef _WIN32
	fd_set fdset;
	FD_ZERO(&fdset);
	if (mSocket != SOCKET_ERROR)
		FD_SET(mSocket, &fdset);
	if (mSocketv6 != SOCKET_ERROR)
		FD_SET(mSocketv6, &fdset);
	timeval lon;
	lon.tv_sec = REFRESH_SECONDS;
	lon.tv_usec = 0;
	_i32 rc = select((std::max)((int)mSocket, (int)mSocketv6) + 1, &fdset, 0, 0, &lon);
#else
	int rc;
	pollfd conn[2];
	if (mSocket == -1 || mSocketv6 == -1)
	{
		if(mSocket==-1)
			conn[0].fd = mSocketv6;
		else
			conn[0].fd = mSocket;
		conn[0].events = POLLIN;
		conn[0].revents = 0;
		conn[1].revents = 0;
		rc = poll(conn, 1, REFRESH_SECONDS * 1000);
	}
	else
	{
		conn[0].fd = mSocket;
		conn[0].events = POLLIN;
		conn[0].revents = 0;
		conn[1].fd = mSocketv6;
		conn[1].events = POLLIN;
		conn[1].revents = 0;
		rc = poll(conn, 2, REFRESH_SECONDS * 1000);
	}
#endif

	if(rc>0)
	{
		for (size_t s = 0; s < 2; ++s)
		{
#ifdef _WIN32
			SOCKET accept_socket = s == 0 ? mSocket : mSocketv6;
			if (accept_socket == SOCKET_ERROR)
				continue;
			if (!FD_ISSET(accept_socket, &fdset))
				continue;
#else
			if (conn[s].revents == 0)
				continue;
			SOCKET accept_socket = conn[s].fd;
#endif
			SOCKET ns;
			if (accept_socket == mSocket)
			{
				sockaddr_in naddr;
				socklen_t addrsize = sizeof(naddr);
				ns = ACCEPT_CLOEXEC(accept_socket, (sockaddr*)&naddr, &addrsize);
			}
			else
			{
				sockaddr_in6 naddr;
				socklen_t addrsize = sizeof(naddr);
				ns = ACCEPT_CLOEXEC(accept_socket, (sockaddr*)&naddr, &addrsize);
			}
			if (ns != SOCKET_ERROR)
			{
#ifdef __APPLE__
				int optval = 1;
				setsockopt(ns, SOL_SOCKET, SO_NOSIGPIPE, (void*)&optval, sizeof(optval));
#endif
				cs.Enter();
				//Log("New Connection incomming", LL_DEBUG);
				CClientThread *clientthread = new CClientThread(ns, this);
				Server->createThread(clientthread, "file server");
				clientthreads.push_back(clientthread);
				cs.Leave();
			}
		}
	}
	else if(rc==SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Select error in CTCPFileServ::TcpStep", LL_ERROR);
#endif
		return false;
	}
	return true;
}

void CTCPFileServ::DelClientThreads(void)
{
	cs.Enter();
	bool proc=true;
	while(proc==true)
	{
		proc=false;
		for(size_t i=0;i<clientthreads.size();++i)
		{
			if(clientthreads[i]->isKillable())
			{
				delete clientthreads[i];
				clientthreads.erase( clientthreads.begin()+i );
				proc=true;
				Log("ClientThread deleted. "+convert((NBUFFERS*READSIZE)/1024)+" KB Memory freed.",LL_DEBUG);
				break;
			}
		}
	}
	cs.Leave();
}

bool CTCPFileServ::startIpv4(_u16 tcpport)
{
	int type = SOCK_STREAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	mSocket = socket(AF_INET, type, 0);
	if (mSocket == SOCKET_ERROR)
	{
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL)
		{
			type &= ~SOCK_CLOEXEC;
			mSocket = socket(AF_INET, type, 0);
		}
#endif
		if (mSocket == SOCKET_ERROR)
		{
			return false;
		}
	}

	if (!setSocketSettings(mSocket))
	{
		closesocket(mSocket);
		mSocket = SOCKET_ERROR;
		return false;
	}

	sockaddr_in addr;

	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(tcpport);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int rc = bind(mSocket, (sockaddr*)& addr, sizeof(addr));
	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Binding tcp socket to port " + convert(tcpport) + " failed. Another instance of this application may already be active and bound to this port.", LL_ERROR);
#else
		Log("Failed. Binding tcp socket.", LL_ERROR);
#endif
		closesocket(mSocket);
		mSocket = SOCKET_ERROR;
		return false;
	}

	rc = listen(mSocket, 60);
	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Binding tcp socket to port " + convert(tcpport) + " failed (listen). Another instance of this application may already be active and bound to this port.", LL_ERROR);
#else
		Log("Failed. Listen to tcp socket.", LL_ERROR);
#endif
		closesocket(mSocket);
		mSocket = SOCKET_ERROR;
		return false;
	}

	return true;
}

bool CTCPFileServ::startIpv6(_u16 tcpport)
{
	int type = SOCK_STREAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	mSocketv6 = socket(AF_INET6, type, 0);
	if (mSocketv6 < 1) return false;

	if (!setSocketSettings(mSocketv6))
	{
		closesocket(mSocketv6);
		mSocketv6 = SOCKET_ERROR;
		return false;
	}

	int optval = 1;
	setsockopt(mSocketv6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)& optval, sizeof(optval));

	sockaddr_in6 addr;

	memset(&addr, 0, sizeof(sockaddr_in6));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(tcpport);
	addr.sin6_addr = in6addr_any;

	int rc = bind(mSocketv6, (sockaddr*)& addr, sizeof(addr));
	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Binding tcp ipv6 socket to port " + convert(tcpport) + " failed. Another instance of this application may already be active and bound to this port.", LL_ERROR);
#else
		Log("Failed. Binding ipv6 tcp socket.", LL_ERROR);
#endif
		closesocket(mSocketv6);
		mSocketv6 = SOCKET_ERROR;
		return false;
	}

	rc = listen(mSocketv6, 60);

	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Binding tcp ipv6 socket to port " + convert(tcpport) + " failed (listen). Another instance of this application may already be active and bound to this port.", LL_ERROR);
#else
		Log("Failed. Listen to ipv6 tcp socket.", LL_ERROR);
#endif
		closesocket(mSocketv6);
		mSocketv6 = SOCKET_ERROR;
		return false;
	}

	return true;
}

bool CTCPFileServ::getUseFQDN()
{
	return m_use_fqdn;
}
