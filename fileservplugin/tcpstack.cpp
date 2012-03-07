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

#pragma warning ( disable:4005 )
#pragma warning ( disable:4996 )

#include "../vld.h"
#ifndef _WIN32
#include <memory.h>
#endif
#include "tcpstack.h"
#include "data.h"
#include "socket_header.h"

#include "../Interface/Pipe.h"

#define MAX_PACKET 4096
#define SEND_TIMEOUT 10000

void CTCPStack::AddData(char* buf, size_t datasize)
{
	size_t osize=buffer.size();
	buffer.resize(osize+datasize);
	memcpy(&buffer[osize], buf, datasize);
}

int CTCPStack::Send(SOCKET sock, char* buf, size_t msglen)
{
	char* buffer=new char[msglen+sizeof(MAX_PACKETSIZE)];

	MAX_PACKETSIZE len=(MAX_PACKETSIZE)msglen;

	memcpy(buffer, &len, sizeof(MAX_PACKETSIZE) );
	memcpy(&buffer[sizeof(MAX_PACKETSIZE)], buf, msglen);

	int rc=send(sock, buffer, (int)msglen+sizeof(MAX_PACKETSIZE), MSG_NOSIGNAL);

	delete[] buffer;

	return rc;
}

int  CTCPStack::Send(SOCKET sock, CWData data)
{
	return Send(sock, data.getDataPtr(), data.getDataSize() );
}

int CTCPStack::Send(IPipe* p, char* buf, size_t msglen)
{
	char* buffer=new char[msglen+sizeof(MAX_PACKETSIZE)];

	MAX_PACKETSIZE len=(MAX_PACKETSIZE)msglen;

	memcpy(buffer, &len, sizeof(MAX_PACKETSIZE) );
	if(msglen>0)
	{
	    memcpy(&buffer[sizeof(MAX_PACKETSIZE)], buf, msglen);
	}

	size_t currpos=0;

	while(currpos<msglen+sizeof(MAX_PACKETSIZE))
	{
		size_t ts=(std::min)((size_t)MAX_PACKET, msglen+sizeof(MAX_PACKETSIZE)-currpos);
		bool b=p->Write(&buffer[currpos], ts, SEND_TIMEOUT );
		currpos+=ts;
		if(!b)
		{
			delete[] buffer;
			return 0;
		}
	}
	delete[] buffer;

	return (int)msglen;
}

int CTCPStack::Send(IPipe* pipe, CWData data)
{
	return Send(pipe, data.getDataPtr(), data.getDataSize() );
}


char* CTCPStack::getPacket(size_t* packetsize)
{
	if(buffer.size()>1)
	{
		MAX_PACKETSIZE len;
		memcpy(&len, &buffer[0], sizeof(MAX_PACKETSIZE) );

		if(buffer.size()>=(size_t)len+sizeof(MAX_PACKETSIZE))
		{
			char* buf=new char[len];
			memcpy(buf, &buffer[sizeof(MAX_PACKETSIZE)], len);

			(*packetsize)=len;

			buffer.erase(buffer.begin(), buffer.begin()+len+sizeof(MAX_PACKETSIZE));

			return buf;
		}
	}
	return NULL;
}

void CTCPStack::reset(void)
{
        buffer.clear();
}
