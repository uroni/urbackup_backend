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

#define REFRESH_SECONDS 1

CTCPFileServ::CTCPFileServ(void)
{
	udpthread=NULL;
	udpticket=ILLEGAL_THREADPOOL_TICKET;
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
	closesocket(mSocket);
}

void CTCPFileServ::KickClients()
{
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

bool CTCPFileServ::Start(_u16 tcpport,_u16 udpport, std::string pServername)
{
	m_tcpport=tcpport;
	m_udpport=udpport;
	_i32 rc;
#ifdef _WIN32
	WSADATA wsadata;
	rc = WSAStartup(MAKEWORD(2,2), &wsadata);
	if(rc == SOCKET_ERROR)	return false;
#endif

	//start tcpsock
	{
		mSocket=socket(AF_INET,SOCK_STREAM,0);
		if(mSocket<1) return false;

#ifndef DISABLE_WINDOW_SIZE
		//Set window size
		int window_size=WINDOW_SIZE;
		int err=setsockopt(mSocket, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size));

		if( err==SOCKET_ERROR )
			Log("Error: Can't modify SO_SNDBUF");
		else 
			Log("Info: retval %i", err );


		err=setsockopt(mSocket, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size));

		if( err==SOCKET_ERROR )
			Log("Error: Can't modify SO_RCVBUF");
		else 
			Log("Info: retval %i", err );

		socklen_t window_size_len=sizeof(window_size);
		getsockopt(mSocket, SOL_SOCKET, SO_SNDBUF,(char *) &window_size, &window_size_len );
		Log("Info: Window size=%i", window_size);
#endif

		sockaddr_in addr;

		memset(&addr, 0, sizeof(sockaddr_in));
		addr.sin_family=AF_INET;
		addr.sin_port=htons(tcpport);
		addr.sin_addr.s_addr=INADDR_ANY;

		rc=bind(mSocket,(sockaddr*)&addr,sizeof(addr));
		if(rc==SOCKET_ERROR)
		{
#ifdef LOG_SERVER
			Server->Log("Binding tcp socket to port "+nconvert(tcpport)+" failed", LL_ERROR);
#else
			Log("Failed. Binding tcp socket.");
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
	if(udpthread==NULL)
	{
		udpthread=new CUDPThread(udpport,pServername);
		if(!udpthread->hasError())
		{
			udpticket=Server->getThreadPool()->execute(udpthread);
		}
		else
		{
			delete udpthread;
			udpthread=NULL;
			Log("Error starting UDP thread");
			return false;
		}
	}

	Log("Server started up sucessfully");

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
	fd_set fdset;
	socklen_t addrsize=sizeof(sockaddr_in);

	FD_ZERO(&fdset);

	FD_SET(mSocket, &fdset);

	timeval lon;
	
	lon.tv_sec=REFRESH_SECONDS;
	lon.tv_usec=0;

	_i32 rc = select((int)mSocket+1, &fdset, 0, 0, &lon);

	if(rc>0)
	{
		sockaddr_in naddr;
		SOCKET ns=accept(mSocket, (sockaddr*)&naddr, &addrsize);
		if(ns>0)
		{
			cs.Enter();
			Log("New Connection incomming");
			CClientThread *clientthread=new CClientThread(ns, this);
			Server->createThread(clientthread);
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
			if(clientthreads[i]->isKillable()==true)
			{
				delete clientthreads[i];
				clientthreads.erase( clientthreads.begin()+i );
				proc=true;
				Log("ClientThread deleted. %i KB Memory freed.",(NBUFFERS*READSIZE)/1024);
				break;
			}
		}
	}
	cs.Leave();
}
