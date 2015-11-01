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

#ifndef TCPSTACK_H
#define TCPSTACK_H

#include <WinSock2.h>
#include <vector>

#define MAX_PACKETSIZE	unsigned int

class CWData;

class CTCPStack
{
public:
	void AddData(char* buf, size_t datasize);

	char* getPacket(size_t* packsize);

	size_t Send(SOCKET p, char* buf, size_t msglen);
	size_t Send(SOCKET p, const std::string &msg);

    void reset(void);

private:
	
	std::vector<char> buffer;
};

#endif //TCPSTACK_H