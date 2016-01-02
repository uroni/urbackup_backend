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

#include "socket_header.h"
#include "LoadbalancerClient.h"
#include "Server.h"
#include <memory.h>
#include "LookupService.h"

CLoadbalancerClient::CLoadbalancerClient(std::string pLB, unsigned short pLBPort, int pWeight, unsigned short pServerport)
{
	lb=pLB;
	lbport=pLBPort;
	weight=pWeight;
	serverport=pServerport;
}

void CLoadbalancerClient::operator ()(void)
{
	int rc;
#ifdef _WIN32
	WSADATA wsadata;
	rc = WSAStartup(MAKEWORD(2,2), &wsadata);
	if(rc == SOCKET_ERROR)	return;
#endif
	SOCKET s=socket(AF_INET,SOCK_STREAM,0);
	if(s<1)
	{
		Server->Log("Creating SOCKET failed (LB)",LL_ERROR);
		return;
	}

	sockaddr_in addr;

	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family=AF_INET;
	addr.sin_port=htons(lbport);
	if(!LookupBlocking(lb, &addr.sin_addr))
	{
		Server->Log("Cannot resolve \""+lb+"\"",LL_ERROR);
		return;
	}

	int err=connect(s, (sockaddr*)&addr, sizeof( sockaddr_in ) );
	
	if( err==-1 )
	{
		Server->Log("Could not connect to LoadBalancer", LL_ERROR );
		closesocket(s);
		return;
	}

	Server->Log("Connected successfully to LoadBalancer", LL_INFO);
	
	char msg[1+sizeof(int)+sizeof(unsigned short)];
	msg[0]=2;
	memcpy( &msg[1], &weight, sizeof(int) );
	memcpy( &msg[1+sizeof(int)], &serverport, sizeof(unsigned short));
	
	send( s, msg, 1+sizeof(int)+sizeof(unsigned short), MSG_NOSIGNAL);
	
	while(true)
	{
		char buffer[100];
		int rc=recv(s, buffer, 100, 0);
		if( rc>0 )
		{
			if( buffer[0]==32 )
			{
				Server->Log("PONG", LL_INFO);
				send( s, buffer, rc, MSG_NOSIGNAL);
			}
		}
		else
		{
			closesocket(s);
			Sleep(50);
			this->operator()();
		}		
	}
}

