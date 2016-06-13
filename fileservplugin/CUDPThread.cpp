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

std::string getSystemServerName(bool use_fqdn)
{
	char hostname[MAX_PATH];
    _i32 rc=gethostname(hostname, MAX_PATH);

	if(rc==SOCKET_ERROR)
	{
		return "_error_";
	}

	if(!use_fqdn)
	{
		return hostname;
	}

	struct hostent* h;
	h = gethostbyname(hostname);
	if(h!=NULL)
	{
		if(strlower(h->h_name)==strlower(hostname))
		{
			return hostname;
		}
		else
		{
			return h->h_name;
		}
	}
	else
	{
		return hostname;
	}
}

bool CUDPThread::hasError(void)
{
	return has_error;
}

CUDPThread::CUDPThread(_u16 udpport,std::string servername, bool use_fqdn)
{
	init(udpport, servername, use_fqdn);
}

void CUDPThread::init(_u16 udpport,std::string servername, bool use_fqdn)
{
	has_error=false;
	do_stop=false;
	udpport_=udpport;
	use_fqdn_=use_fqdn;

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

		Log("Binding UDP socket at port "+nconvert(udpport)+"...", LL_DEBUG);
		rc=bind(udpsock, (sockaddr*)&addr_udp, sizeof(sockaddr_in));
		if(rc==SOCKET_ERROR)
		{
#ifdef LOG_SERVER
			Server->Log("Binding UDP socket to port "+nconvert(udpport)+" failed", LL_ERROR);
#else
			Log("Failed binding UDP socket.", LL_ERROR);
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

	do 
	{
		while(UdpStep()==true && do_stop==false);


		if(!do_stop && has_error)
		{
			Log("Trying to restart UDP listening...", LL_DEBUG);

			if(udpsock!=SOCKET_ERROR)
			{
				closesocket(udpsock);
			}

			init(udpport_, mServername, use_fqdn_);

			if(has_error)
			{
				break;
			}

			if(!UdpStep())
			{
				break;
			}
		}
		else
		{
			break;
		}

	} while (true);
	
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
#ifdef _WIN32
			Server->Log("Last error: "+ nconvert((int)GetLastError()), LL_ERROR);
#endif
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
#ifdef _WIN32
			Server->Log("Last error: "+ nconvert((int)GetLastError()), LL_ERROR);
#endif
#endif
		has_error=true;
		return false;
	}

	return true;
}

void CUDPThread::stop(void)
{
	do_stop=true;
	Log("Stopping CUPDThread...", LL_DEBUG);
	closesocket(udpsock);
}
