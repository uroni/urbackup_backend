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

#include "tcpstack.h"
#include "data.h"
#include <memory.h>

#define SEND_TIMEOUT 10000
#define MAX_PACKET 4096

void CTCPStack::AddData(char* buf, size_t datasize)
{
	if(datasize>0)
	{
		size_t osize=buffer.size();
		buffer.resize(osize+datasize);
		memcpy(&buffer[osize], buf, datasize);
	}
}

size_t CTCPStack::Send(IPipe* p, char* buf, size_t msglen)
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

	return msglen;
}

size_t  CTCPStack::Send(IPipe* p, CWData data)
{
	return Send(p, data.getDataPtr(), data.getDataSize() );
}

size_t CTCPStack::Send(IPipe* p, const std::string &msg)
{
	return Send(p, (char*)msg.c_str(), msg.size());
}


char* CTCPStack::getPacket(size_t* packetsize)
{
	if(buffer.size()>=sizeof(MAX_PACKETSIZE))
	{
		MAX_PACKETSIZE len;
		memcpy(&len, &buffer[0], sizeof(MAX_PACKETSIZE) );

		if(buffer.size()>=(size_t)len+sizeof(MAX_PACKETSIZE))
		{
			char* buf=new char[len+1];
			if(len>0)
			{
				memcpy(buf, &buffer[sizeof(MAX_PACKETSIZE)], len);
			}

			(*packetsize)=len;

			buffer.erase(buffer.begin(), buffer.begin()+len+sizeof(MAX_PACKETSIZE));

	                buf[len]=0;

			return buf;
		}
	}
	return NULL;
}

void CTCPStack::reset(void)
{
        buffer.clear();
}

char *CTCPStack::getBuffer()
{
	return &buffer[0];
}

size_t CTCPStack::getBuffersize()
{
	return buffer.size();
}