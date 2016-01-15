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
#include "log.h"
#include <memory.h>

#include <iostream>

#define REFRESH_SECONDS 10

CTCPFileServ::CTCPFileServ(void)
{
	udpthread=NULL;
	udpticket=ILLEGAL_THREADPOOL_TICKET;
	m_use_fqdn=false;
}

CTCPFileServ::~CTCPFileServ(void)
{
	if(udpthread!=NULL)
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
	closesocket(mSocket);

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
		int type = SOCK_STREAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
		type |= SOCK_CLOEXEC;
#endif
		mSocket=socket(AF_INET, type, 0);
		if(mSocket<1) return false;

#ifndef DISABLE_WINDOW_SIZE
		//Set window size
		int window_size=WINDOW_SIZE;
		int err=setsockopt(mSocket, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size));

		if( err==SOCKET_ERROR )
			Log("Error: Can't modify SO_SNDBUF", LL_DEBUG);


		err=setsockopt(mSocket, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size));

		if( err==SOCKET_ERROR )
			Log("Error: Can't modify SO_RCVBUF", LL_DEBUG);
#endif
		int optval=1;
		rc=setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
		if(rc==SOCKET_ERROR)
		{
			Log("Failed setting SO_REUSEADDR in CTCPFileServ::Start", LL_ERROR);
			return false;
		}

		sockaddr_in addr;

		memset(&addr, 0, sizeof(sockaddr_in));
		addr.sin_family=AF_INET;
		addr.sin_port=htons(tcpport);
		addr.sin_addr.s_addr=INADDR_ANY;

		rc=bind(mSocket,(sockaddr*)&addr,sizeof(addr));
		if(rc==SOCKET_ERROR)
		{
#ifdef LOG_SERVER
			Server->Log("Binding tcp socket to port "+convert(tcpport)+" failed. Another instance of this application may already be active and bound to this port.", LL_ERROR);
#else
			Log("Failed. Binding tcp socket.", LL_ERROR);
#endif
			return false;
		}

		listen(mSocket,60);
	}
	//start udpsock
	if(udpthread!=NULL && udpthread->hasError() )
	{
		delete udpthread;
		udpthread=NULL;
	}
	if(udpthread==NULL && udpport!=0)
	{
		udpthread=new CUDPThread(udpport,pServername, use_fqdn);
		if(!udpthread->hasError())
		{
			udpticket=Server->getThreadPool()->execute(udpthread, "filesrv: broadcast response");
		}
		else
		{
			delete udpthread;
			udpthread=NULL;
			Log("Error starting UDP thread", LL_ERROR);
			return false;
		}
	}

	Log("Server started up sucessfully", LL_DEBUG);

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
	if(m_tcpport==0)
	{
		Server->wait(REFRESH_SECONDS*1000);
		return true;
	}

	socklen_t addrsize=sizeof(sockaddr_in);

#ifdef _WIN32
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(mSocket, &fdset);
	timeval lon;	
	lon.tv_sec=REFRESH_SECONDS;
	lon.tv_usec=0;
	_i32 rc = select((int)mSocket+1, &fdset, 0, 0, &lon);
#else
	pollfd conn[1];
	conn[0].fd=mSocket;
	conn[0].events=POLLIN;
	conn[0].revents=0;
	int rc = poll(conn, 1, REFRESH_SECONDS*1000);
#endif

	if(rc>0)
	{
		sockaddr_in naddr;
		SOCKET ns=accept(mSocket, (sockaddr*)&naddr, &addrsize);
		if(ns>0)
		{
			cs.Enter();
			//Log("New Connection incomming", LL_DEBUG);
			CClientThread *clientthread=new CClientThread(ns, this);
			Server->createThread(clientthread, "file server");
			clientthreads.push_back(clientthread);
			cs.Leave();
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

bool CTCPFileServ::getUseFQDN()
{
	return m_use_fqdn;
}
