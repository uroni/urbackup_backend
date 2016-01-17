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

#include "AcceptThread.h"
#include "Server.h"
#include "stringtools.h"
#include "SelectThread.h"
#include "Client.h"
#include <memory.h>
#ifndef _WIN32
#include <errno.h>
#endif

extern bool run;

OutputCallback::OutputCallback(SOCKET fd_)
{
	  fd=fd_;
}

OutputCallback::~OutputCallback()
{
    closesocket(fd);
}

void OutputCallback::operator() (const void* buf, size_t count)
{
	size_t sent=0;
	do
	{
		int rc = send(fd, (const char*)buf+sent, static_cast<int>(count-sent), MSG_NOSIGNAL);
		if (rc < 0)
		{
			int ec;
	#ifdef _WIN32
			ec=GetLastError();
	#else
			ec=errno;
	#endif
			Server->Log("Send failed in OutputCallback ec="+convert(ec), LL_INFO);
			throw std::runtime_error("Send failed in OutputCallback");
		}
		else
		{
			sent+=rc;
		}
	}
	while(sent<count);
}

CAcceptThread::CAcceptThread( unsigned int nWorkerThreadsPerMaster, unsigned short int uPort ) : error(false)
{
	WorkerThreadsPerMaster=nWorkerThreadsPerMaster;

	int type = SOCK_STREAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	s=socket(AF_INET, type, 0);
	if(s<1)
	{
		Server->Log("Creating SOCKET failed. Port "+convert((int)uPort)+" may already be in use",LL_ERROR);
		error=true;
		return;
	}

	int optval=1;
	int rc=setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
	if(rc==SOCKET_ERROR)
	{
		Server->Log("Failed setting SO_REUSEADDR for port "+convert(uPort),LL_ERROR);
		error=true;
		return;
	}
#ifdef __APPLE__
	optval = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (void*)&optval, sizeof(optval));
#endif

	sockaddr_in addr;

	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(uPort);
	addr.sin_addr.s_addr=INADDR_ANY;

	rc=bind(s,(sockaddr*)&addr,sizeof(addr));
	if(rc==SOCKET_ERROR)
	{
		Server->Log("Failed binding SOCKET to Port "+convert(uPort),LL_ERROR);
		error=true;
		return;
	}

	listen(s, 10000);

	Server->Log("Server started up sucessfully!",LL_INFO);
}

CAcceptThread::~CAcceptThread()
{
	closesocket(s);
	Server->Log("Deleting SelectThreads..");
	for(size_t i=0;i<SelectThreads.size();++i)
	{
		delete SelectThreads[i];
	}
}

#ifndef _WIN32
void printLinError(void)
{
	switch(errno)
	{
		case EWOULDBLOCK: Server->Log("Reason: EWOULDBLOCK", LL_ERROR); break;
		case EBADF: Server->Log("Reason: EBADF", LL_ERROR); break;
		case ECONNABORTED: Server->Log("Reason: ECONNABORTED", LL_ERROR); break;
		case EINTR: Server->Log("Reason: EINTR", LL_ERROR); break;
		case EINVAL: Server->Log("Reason: EINVAL", LL_ERROR); break;
		case EMFILE: Server->Log("Reason: EMFILE", LL_ERROR); break;
		case ENFILE: Server->Log("Reason: ENFILE", LL_ERROR); break;
		case ENOTSOCK: Server->Log("Reason: ENOTSOCK", LL_ERROR); break;
		case EFAULT: Server->Log("Reason: EFAULT", LL_ERROR); break;
		case ENOBUFS: Server->Log("Reason: ENOBUFS", LL_ERROR); break;
		case ENOMEM: Server->Log("Reason: ENOMEM", LL_ERROR); break;
		case EPROTO: Server->Log("Reason: EPROTO", LL_ERROR); break;
		case EPERM: Server->Log("Reason: EPERM", LL_ERROR); break;
	}
}
#endif

void CAcceptThread::operator()(bool single)
{
	do
	{
		socklen_t addrsize=sizeof(sockaddr_in);

#ifdef _WIN32
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(s, &fdset);

		timeval lon;	
		lon.tv_sec=1;
		lon.tv_usec=0;

		_i32 rc=select((int)s+1, &fdset, 0, 0, &lon);

		if( rc<0 )
			return;

		if( FD_ISSET(s,&fdset) )
		{
#else
		pollfd conn[1];
		conn[0].fd=s;
		conn[0].events=POLLIN;
		conn[0].revents=0;
		int rc = poll(conn, 1, 1000);
		if(rc<0)
			return;

		if(rc>0)
		{
#endif		
			sockaddr_in naddr;
			SOCKET ns=accept(s, (sockaddr*)&naddr, &addrsize);
			if(ns!=SOCKET_ERROR)
			{

#ifdef __APPLE__
				int optval = 1;
				setsockopt(ns, SOL_SOCKET, SO_NOSIGPIPE, (void*)&optval, sizeof(optval));
#endif
				//Server->Log("New Connection incomming", LL_INFO);

				OutputCallback *output=new OutputCallback(ns);
				FCGIProtocolDriver *driver=new FCGIProtocolDriver(*output );

				CClient *client=new CClient();
				client->set(ns, output, driver);

				AddToSelectThread(client);
			}
			else
			{
				Server->Log("Accepting client failed", LL_ERROR);
#ifndef _WIN32
				printLinError();
#endif
				Server->wait(1000);
			}
		}
	}while(single==false);
}

void CAcceptThread::AddToSelectThread(CClient *client)
{
	for(size_t i=0;i<SelectThreads.size();++i)
	{
		if( SelectThreads[i]->FreeClients()>0 )
		{
			SelectThreads[i]->AddClient( client );
			return;
		}
	}

	CSelectThread *nt=new CSelectThread(WorkerThreadsPerMaster);
	nt->AddClient( client );

	SelectThreads.push_back( nt );

	Server->createThread(nt, "fastcgi: accept");
}

bool CAcceptThread::has_error(void)
{
	return error;
}
