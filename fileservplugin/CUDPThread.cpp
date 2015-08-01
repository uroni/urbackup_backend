/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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

#pragma warning ( disable:4005 )
#pragma warning ( disable:4996 )

#ifdef _WIN32
#include <winsock2.h>
#endif

#include "../vld.h"
#include "../Interface/Server.h"
#include "CUDPThread.h"
#include "settings.h"
#include "packet_ids.h"
#include "log.h"
#include "FileServ.h"
#include "../stringtools.h"
#include <memory.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#endif

std::string getSystemServerName(bool use_fqdn)
{
	char hostname[MAX_PATH];
#ifdef __APPLE__
	//TODO: Fix FQDN for Apple
	FILE* fd=popen("/bin/hostname", "r");
	if(fd!=NULL)
	{
		if(fgets(hostname, MAX_PATH, fd)!=NULL)
		{
			return trim(hostname);
		}
		pclose(fd);
	}
#endif

    _i32 rc=gethostname(hostname, MAX_PATH);

	if(rc==SOCKET_ERROR)
	{
		return "_error_";
	}

	if(!use_fqdn)
	{
		return hostname;
	}

	std::string ret;

	addrinfo* h;
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	if(getaddrinfo(hostname, NULL, NULL, &h)==0)
	{
		if(h->ai_canonname==NULL ||
			strlower(h->ai_canonname)==strlower(hostname))
		{
			ret = hostname;
		}
		else
		{
			ret = h->ai_canonname;
		}

		freeaddrinfo(h);
	}
	else
	{
		ret = hostname;
	}

	return ret;
}

bool CUDPThread::hasError(void)
{
	return has_error;
}

CUDPThread::CUDPThread(_u16 udpport,std::string servername, bool use_fqdn)
{
	has_error=false;
	do_stop=false;
	{
		udpsock=socket(AF_INET,SOCK_DGRAM,0);

		int optval=1;
		int rc=setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
		if(rc==SOCKET_ERROR)
		{
			Log("Failed setting SO_REUSEADDR in CUDPThread::CUDPThread", LL_ERROR);
			return;
		}

		sockaddr_in addr_udp;

		addr_udp.sin_family=AF_INET;
		addr_udp.sin_port=htons(udpport);
		addr_udp.sin_addr.s_addr=INADDR_ANY;

		Log("Binding udp socket at port "+nconvert(udpport)+"...", LL_DEBUG);
		rc=bind(udpsock, (sockaddr*)&addr_udp, sizeof(sockaddr_in));
		if(rc==SOCKET_ERROR)
		{
#ifdef LOG_SERVER
			Server->Log("Binding udp socket to port "+nconvert(udpport)+" failed", LL_ERROR);
#else
			Log("Failed binding udp socket.", LL_ERROR);
#endif
			has_error=true;
			return;
		}
		Log("done.", LL_DEBUG);

#ifdef _WIN32
		DWORD dwBytesReturned = 0;
		BOOL bNewBehavior = FALSE;
		DWORD status;

		Log("Disabling new behavior...", LL_DEBUG);
		// disable  new behavior using
		// IOCTL: SIO_UDP_CONNRESET
		#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
		status = WSAIoctl(udpsock, SIO_UDP_CONNRESET,
						&bNewBehavior, sizeof(bNewBehavior),
					NULL, 0, &dwBytesReturned,
					NULL, NULL);
		if (SOCKET_ERROR == status)
		{
			Log("Setting SIO_UDP_CONNRESET via WSAIoctl FAILED!", LL_WARNING);
			//return;
		}
#endif
	}

	if( servername!="" )
		mServername=servername;
	else
		mServername=getSystemServerName(use_fqdn);

	Log("Servername: -"+mServername+"-", LL_INFO);
}

std::string CUDPThread::getServername()
{
	return mServername;
}

CUDPThread::~CUDPThread()
{
	if(udpsock!=SOCKET_ERROR)
	{
		closesocket(udpsock);
	}
}

void CUDPThread::operator()(void)
{
	Log("UDP Thread startet", LL_DEBUG);
#ifdef _WIN32
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

	while(UdpStep()==true && do_stop==false);
	
#ifdef LOG_SERVER
	Server->Log("CUDPThread exited.", LL_DEBUG);
#endif
	if(do_stop)
	{
		delete this;
	}
}

bool CUDPThread::UdpStep(void)
{
	if(has_error)
		return false;

	int rc;
	if(!do_stop)
	{
#ifdef _WIN32
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(udpsock, &fdset);

		timeval lon;
		memset(&lon,0,sizeof(timeval) );
		lon.tv_sec=60;
		rc=select((int)udpsock+1, &fdset, 0, 0, &lon);		
#else
		pollfd conn[1];
		conn[0].fd=udpsock;
		conn[0].events=POLLIN;
		conn[0].revents=0;
		rc = poll(conn, 1, 60*1000);
#endif
	}
	else
	{
		rc=SOCKET_ERROR;
	}

	if(rc>0)
	{
		//Log("Receiving UDP packet...");
		char buffer[BUFFERSIZE];
		socklen_t addrsize=sizeof(sockaddr_in);
		sockaddr_in sender = {};
		_i32 err = recvfrom(udpsock, buffer, BUFFERSIZE, 0, (sockaddr*)&sender, &addrsize);
		if(err==SOCKET_ERROR)
		{
#ifdef LOG_SERVER
			Server->Log("Recvfrom error in CUDPThread::UdpStep", LL_ERROR);
#endif
			has_error=true;
			return false;
		}
		else if(err>0)
		{
			if(buffer[0]==ID_PING)
			{
				unsigned int rsleep=Server->getRandomNumber()%500;
				Log("UDP: PING received... sending PONG. Delay="+nconvert(rsleep)+"ms", LL_DEBUG);
				Server->wait(rsleep);
				char *buffer=new char[mServername.size()+2];
				buffer[0]=ID_PONG;
				buffer[1]=VERSION;
				memcpy(&buffer[2], mServername.c_str(), mServername.size());
				int rc=sendto(udpsock, buffer, (int)mServername.size()+2, 0, (sockaddr*)&sender, addrsize );
				if( rc==SOCKET_ERROR )
				{
					Log("Sending reply failed "+nconvert(rc), LL_DEBUG);
				}
				delete[] buffer;
			}
			else
			{
#ifdef LOG_SERVER
				Server->Log("Unknown UDP packet id", LL_WARNING);
#endif
			}
		}
	}
	else if( rc==SOCKET_ERROR )
	{
#ifdef LOG_SERVER
			Server->Log("Select error in CUDPThread::UdpStep", LL_ERROR);
#endif
		has_error=true;
		return false;
	}

	return true;
}

void CUDPThread::stop(void)
{
	Log("Stopping CUPDThread...", LL_DEBUG);
	closesocket(udpsock);
	do_stop=true;
}