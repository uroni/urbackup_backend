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
#include <string>
#ifndef _WIN32
#include <memory.h>
#include "LookupService.h"
#endif
#include "LookupService.h"

std::vector<SLookupBlockingResult> LookupBlocking(std::string pServer)
{
	const char* host = pServer.c_str();
	unsigned int addr = inet_addr(host);
	if (addr != INADDR_NONE)
	{
		SLookupBlockingResult res;
		res.zone = 0;
		res.is_ipv6 = false;
		res.addr_v4 = addr;
		std::vector<SLookupBlockingResult> ret;
		ret.push_back(res);
		return ret;
	}

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	std::vector<SLookupBlockingResult> ret;
	addrinfo* h;
	if (getaddrinfo(pServer.c_str(), NULL, &hints, &h) == 0)
	{
		addrinfo* orig_h = h;
		while (h != NULL)
		{
			if (h->ai_family != AF_INET6
				&& h->ai_family != AF_INET)
			{
				h = h->ai_next;
				continue;
			}

			if (h->ai_family == AF_INET6)
			{
				if (h->ai_addrlen >= sizeof(sockaddr_in6))
				{
					SLookupBlockingResult dest;
					dest.is_ipv6 = true;
					memcpy(dest.addr_v6, &reinterpret_cast<sockaddr_in6*>(h->ai_addr)->sin6_addr, 16);
					dest.zone = reinterpret_cast<sockaddr_in6*>(h->ai_addr)->sin6_scope_id;
					ret.push_back(dest);
				}
			}
			else
			{
				if (h->ai_addrlen >= sizeof(sockaddr_in))
				{
					SLookupBlockingResult dest;
					dest.zone = 0;
					dest.is_ipv6 = false;
					dest.addr_v4 = reinterpret_cast<sockaddr_in*>(h->ai_addr)->sin_addr.s_addr;
					ret.push_back(dest);
				}
			}

			h = h->ai_next;
		}
		freeaddrinfo(orig_h);
		return ret;
	}
	else
	{
		return ret;
	}
}

bool LookupHostname(const std::string & pIp, std::string& hostname)
{
	sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = inet_addr(pIp.c_str());
	if (addr.sin_addr.s_addr == INADDR_NONE)
	{
		return false;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(80);

	char buf[NI_MAXHOST];

	int rc = getnameinfo(reinterpret_cast<sockaddr*>(&addr), sizeof(addr), buf, sizeof(buf), NULL, 0, NI_NAMEREQD);

	if (rc == 0)
	{
		hostname = buf;
	}

	return rc == 0;
}




