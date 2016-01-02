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

#include "tcpstack.h"
#include <memory.h>
#include "../socket_header.h"


void CTCPStack::AddData(char* buf, size_t datasize)
{
	if(datasize>0)
	{
		size_t osize=buffer.size();
		buffer.resize(osize+datasize);
		memcpy(&buffer[osize], buf, datasize);
	}
}

size_t CTCPStack::Send(SOCKET p, char* buf, size_t msglen)
{
	char* buffer=new char[msglen+sizeof(MAX_PACKETSIZE)];

	MAX_PACKETSIZE len=(MAX_PACKETSIZE)msglen;

	memcpy(buffer, &len, sizeof(MAX_PACKETSIZE) );
	memcpy(&buffer[sizeof(MAX_PACKETSIZE)], buf, msglen);

	int rc=send(p, buffer, (int)msglen+sizeof(MAX_PACKETSIZE), 0);

	delete[] buffer;

	return (size_t)rc;
}

size_t CTCPStack::Send(SOCKET p, const std::string &msg)
{
	return Send(p, (char*)msg.c_str(), msg.size());
}


char* CTCPStack::getPacket(size_t* packetsize)
{
	if(buffer.size()>1)
	{
		MAX_PACKETSIZE len;
		memcpy(&len, &buffer[0], sizeof(MAX_PACKETSIZE) );

		if(len==0)
		{
			*packetsize=0;
			buffer.erase(buffer.begin(), buffer.begin()+len+sizeof(MAX_PACKETSIZE));
			return NULL;
		}

		if(buffer.size()>=(size_t)len+sizeof(MAX_PACKETSIZE))
		{
			char* buf=new char[len+1];
			memcpy(buf, &buffer[sizeof(MAX_PACKETSIZE)], len);

			(*packetsize)=len;

			buffer.erase(buffer.begin(), buffer.begin()+len+sizeof(MAX_PACKETSIZE));

            buf[len]=0;

			return buf;
		}
	}
	*packetsize=-1;
	return NULL;
}

void CTCPStack::reset(void)
{
        buffer.clear();
}
