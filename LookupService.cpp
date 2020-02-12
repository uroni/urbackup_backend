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
#include <iostream>
#include <algorithm>
#ifdef ENABLE_C_ARES
#include <ares.h>
#include "Server.h"
#endif

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

#ifdef ENABLE_C_ARES

void addrinfo_callback(void *arg,
	int status,
	int timeouts,
	struct ares_addrinfo *res)
{
	std::vector<SLookupBlockingResult>* ret
		= reinterpret_cast<std::vector<SLookupBlockingResult>*>(arg);

	if (status == ARES_SUCCESS
		&& res!=NULL)
	{
		ares_addrinfo_node* h = res->nodes;
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
					ret->push_back(dest);
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
					ret->push_back(dest);
				}
			}

			h = h->ai_next;
		}
	}
}

bool LookupInit()
{
	int rc = ares_library_init(ARES_LIB_INIT_ALL);
	if (rc != ARES_SUCCESS)
	{
		std::cerr << "C-ares initialization failed with code " << ares_strerror(rc) << std::endl;
		return false;
	}

	return true;
}

std::vector<SLookupBlockingResult> LookupWithTimeout(std::string pServer, int timeoutms, int stop_timeoutms)
{
	if (pServer == "localhost")
	{
		pServer = "127.0.0.1";
	}

	unsigned int addr = inet_addr(pServer.c_str());
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

	ares_channel channel;
	struct ares_options options = {};

	int rc = ares_init_options(&channel, &options, 0);
	if (rc != ARES_SUCCESS)
	{
		return std::vector<SLookupBlockingResult>();
	}

	ares_addrinfo_hints hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	std::vector<SLookupBlockingResult> ret;
	ares_getaddrinfo(channel, pServer.c_str(), NULL, &hints, addrinfo_callback, &ret);
	
	int nfds, count;
	fd_set readers, writers;
	timeval maxtv;
	maxtv.tv_sec = (std::min)(stop_timeoutms, timeoutms) / 1000;
	maxtv.tv_usec = ((std::min)(stop_timeoutms, timeoutms) % 1000) * 1000;

	int64 starttime = Server->getTimeMS();
	do
	{
		ares_socket_t socks[ARES_GETSOCK_MAXNUM];

		rc = ares_getsock(channel, socks, ARES_GETSOCK_MAXNUM);

		pollfd conn[ARES_GETSOCK_MAXNUM];
		bool has_fd = false;
		for (int i = 0; i < ARES_GETSOCK_MAXNUM; ++i)
		{
			conn[i].fd = -1;
			conn[i].events = 0;
			conn[i].revents = 0;
			if (ARES_GETSOCK_READABLE(rc, i))
			{
				has_fd = true;
				conn[i].fd = socks[i];
				conn[i].events = POLLIN;
			}
			if (ARES_GETSOCK_WRITABLE(rc, i))
			{
				has_fd = true;
				conn[i].fd = socks[i];
				conn[i].events |= POLLOUT;
			}
		}

		if (!has_fd)
			break;

		struct timeval tv;
		ares_timeout(channel, &maxtv, &tv);

		rc = poll(conn, ARES_GETSOCK_MAXNUM, tv.tv_sec*1000+tv.tv_usec/1000);
		if (rc == -1)
			break;

		for (int i = 0; i < ARES_GETSOCK_MAXNUM; ++i)
		{
			if (conn[i].revents > 0)
			{
				ares_process_fd(channel, (conn[i].events & POLLIN) ? conn[i].fd : ARES_SOCKET_BAD,
					(conn[i].events & POLLOUT) ? conn[i].fd : ARES_SOCKET_BAD);
			}
		}

		if (!ret.empty()
			&& Server->getTimeMS() - starttime >= stop_timeoutms)
		{
			break;
		}

	} while (Server->getTimeMS() - starttime < timeoutms);

	ares_destroy(channel);

	return ret;
}

#endif

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




