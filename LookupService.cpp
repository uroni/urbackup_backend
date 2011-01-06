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

#include "socket_header.h"
#include <string>
#ifndef _WIN32
#include <memory.h>
#endif

bool LookupBlocking(std::string pServer, in_addr *dest)
{
	const char* host=pServer.c_str();
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE)
	{
        dest->s_addr = addr;
    }
	else
	{
		hostent* hp = gethostbyname(host);
        if (hp != 0)
		{
			memcpy(dest, hp->h_addr, hp->h_length);
		}
		else
		{
			return false;
		}
	}
	return true;
}




