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

#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif

namespace
{
	const char* multicast_group = "ff12::f894:d:dd00:ef91";
}

#ifdef __APPLE__
std::string mac_get_serial()
{
	char buf[4096];
	FILE* fd = popen("system_profiler SPHardwareDataType | grep \"Serial Number\"", "r");
	std::string serial;
	if (fd != NULL)
	{
		if (fgets(buf, sizeof(buf), fd) != NULL)
		{
			serial = trim(getafter(":", buf));
		}
	}

	if (!serial.empty())
	{
		return serial;
	}

	int64 tt = Server->getTimeSeconds() / 60;
	tt = big_endian(tt);
	char* p = reinterpret_cast<char*>(&tt);
	Server->randomFill(p, sizeof(int64)-3);
	serial = bytesToHex(reinterpret_cast<unsigned char*>(p), sizeof(int64));

	return serial;
}
#endif

std::string getSystemServerName(bool use_fqdn)
{
	char hostname[MAX_PATH];
#ifdef __APPLE__
	//TODO: Fix FQDN for Apple
	while (true)
	{
		char hostname_appl[MAX_PATH + 15];
		FILE* fd = popen("system_profiler SPSoftwareDataType | grep \"Computer Name: \"", "r");
		if (fd != NULL)
		{
			if (fgets(hostname_appl, sizeof(hostname_appl), fd) != NULL)
			{
				std::string chostname = getafter("Computer Name: ", trim(hostname_appl));
				if (chostname.empty())
				{
					Server->wait(100);
					continue;
				}

				const std::string mac_add_fn = "urbackup/mac_computername_add.txt";
				std::string mac_add = getStreamFile(mac_add_fn);
				if (!mac_add.empty())
				{
					return chostname + "-" + mac_add;
				}

				mac_add = mac_get_serial();

				writestring(mac_add, mac_add_fn);

				return chostname + "-" + mac_add;
			}
			pclose(fd);
		}
		else
		{
			Server->wait(100);
		}
	}
#else

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
#endif
}

bool CUDPThread::hasError(void)
{
	return has_error;
}

CUDPThread::CUDPThread(_u16 udpport,std::string servername, bool use_fqdn)
	:udpsock(SOCKET_ERROR), udpsockv6(SOCKET_ERROR)
{
	init(udpport, servername, use_fqdn);
}

namespace
{
	void setSocketSettings(SOCKET udpsock)
	{
#if !defined(_WIN32) && !defined(SOCK_CLOEXEC)
		fcntl(udpsock, F_SETFD, fcntl(udpsock, F_GETFD, 0) | FD_CLOEXEC);
#endif
		int optval = 1;
		int rc = setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(int));
		if (rc == SOCKET_ERROR)
		{
			Log("Failed setting SO_REUSEADDR in CUDPThread::CUDPThread", LL_ERROR);
			return;
		}
	}

	void disableNewWindowsBehaviour(SOCKET udpsock)
	{
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
}

void CUDPThread::init(_u16 udpport,std::string servername, bool use_fqdn)
{
	has_error=false;
	do_stop=false;
	udpport_=udpport;
	use_fqdn_=use_fqdn;

	bool has_ipv4 = Server->getServerParameter("disable_ipv4").empty();
	bool has_ipv6 = Server->getServerParameter("disable_ipv6").empty();

	if (!has_ipv4 && !has_ipv6)
	{
		Log("IPv4 and IPv6 disabled", LL_ERROR);
		has_error = true;
		return;
	}

	if (has_ipv4
		&& !init_v4(udpport))
	{
		if (Server->getServerParameter("ipv4_optional").empty()
			|| !has_ipv6)
		{
			has_error = true;
			return;
		}
	}

	if (has_ipv6
		&& !init_v6(udpport))
	{
		if (!Server->getServerParameter("ipv6_required").empty()
			|| !has_ipv4)
		{
			has_error = true;
			return;
		}
	}

	if( servername!="" )
		mServername=servername;
	else
		mServername=getSystemServerName(use_fqdn);

	Log("Servername: -"+mServername+"-", LL_INFO);
}

bool CUDPThread::init_v4(_u16 udpport)
{
	int type = SOCK_DGRAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	udpsock = socket(AF_INET, type, 0);
	if (udpsock == SOCKET_ERROR)
	{
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
		if (errno == EINVAL)
		{
			type &= ~SOCK_CLOEXEC;
			udpsock = socket(AF_INET, type, 0);
		}
#endif
		if (udpsock == SOCKET_ERROR)
		{
			Log("Error creating udpsock in CUDPThread::init_v4", LL_ERROR);
			return false;
		}
	}

	setSocketSettings(udpsock);

	sockaddr_in addr_udp;
	memset(&addr_udp, 0, sizeof(addr_udp));

	addr_udp.sin_family = AF_INET;
	addr_udp.sin_port = htons(udpport);
	addr_udp.sin_addr.s_addr = htonl(INADDR_ANY);

	Log("Binding UDP socket at port " + convert(udpport) + "...", LL_DEBUG);
	int rc = bind(udpsock, (sockaddr*)& addr_udp, sizeof(sockaddr_in));
	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Binding UDP socket to port " + convert(udpport) + " failed", LL_ERROR);
#else
		Log("Failed binding UDP socket.", LL_ERROR);
#endif
		closesocket(udpsock);
		udpsock = SOCKET_ERROR;
		return false;
	}
	Log("done.", LL_DEBUG);

	disableNewWindowsBehaviour(udpsock);

	return true;
}

bool CUDPThread::init_v6(_u16 udpport)
{
	int type = SOCK_DGRAM;
#if !defined(_WIN32) && defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	udpsockv6 = socket(AF_INET6, type, 0);

	setSocketSettings(udpsockv6);

	int optval = 1;
	setsockopt(udpsockv6, IPPROTO_IPV6, IPV6_V6ONLY, (char*)& optval, sizeof(optval));

	sockaddr_in6 addr_udp;
	memset(&addr_udp, 0, sizeof(addr_udp));

	addr_udp.sin6_family = AF_INET6;
	addr_udp.sin6_port = htons(udpport);
	addr_udp.sin6_addr = in6addr_any;

	Log("Binding ipv6 UDP socket at port " + convert(udpport) + "...", LL_DEBUG);

	int rc = bind(udpsockv6, (sockaddr*)& addr_udp, sizeof(addr_udp));
	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log("Binding ipv6 UDP socket to port " + convert(udpport) + " failed", LL_ERROR);
#else
		Log("Failed binding ipv6 UDP socket.", LL_ERROR);
#endif
		return false;
	}
	Log("done.", LL_DEBUG);

	struct ipv6_mreq group;
	group.ipv6mr_interface = 0;
	inet_pton(AF_INET6, multicast_group, &group.ipv6mr_multiaddr);
	rc = setsockopt(udpsockv6, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, reinterpret_cast<char*>(&group), sizeof(group));
	if (rc == SOCKET_ERROR)
	{
#ifdef LOG_SERVER
		Server->Log(std::string("Error joining ipv6 multicast group ") + multicast_group, LL_ERROR);
#else
		Log(std::string("Error joining ipv6 multicast group ") + multicast_group, LL_ERROR);
#endif
		closesocket(udpsockv6);
		udpsockv6 = SOCKET_ERROR;
		return false;
	}

	disableNewWindowsBehaviour(udpsockv6);

	return true;
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
	if (udpsockv6 != SOCKET_ERROR)
	{
		closesocket(udpsockv6);
	}
}

void CUDPThread::operator()(void)
{
	Log("UDP Thread started", LL_DEBUG);
#ifdef _WIN32
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

	do 
	{
		while(UdpStep() && !do_stop);


		if(!do_stop && has_error)
		{
			Log("Trying to restart UDP listening...", LL_DEBUG);

			if(udpsock!=SOCKET_ERROR)
			{
				closesocket(udpsock);
			}

			if (udpsockv6 != SOCKET_ERROR)
			{
				closesocket(udpsockv6);
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

#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
}

namespace
{
	struct DualSockAddr
	{
		union
		{
			sockaddr_in ipv4;
			sockaddr_in6 ipv6;
		};
	};
}

bool CUDPThread::UdpStep(void)
{
	if(has_error)
		return false;

	int rc;
#ifndef _WIN32
	pollfd conn[2];
#else
	fd_set fdset;
#endif
	if(!do_stop)
	{
#ifdef _WIN32
		FD_ZERO(&fdset);
		if(udpsock!=SOCKET_ERROR)
			FD_SET(udpsock, &fdset);
		if(udpsockv6!=SOCKET_ERROR)
			FD_SET(udpsockv6, &fdset);

		timeval lon;
		memset(&lon,0,sizeof(timeval) );
		lon.tv_sec=60;
		rc=select((std::max)((int)udpsock, (int)udpsockv6) +1, &fdset, 0, 0, &lon);
#else
		if (udpsock == SOCKET_ERROR
			|| udpsockv6 == SOCKET_ERROR)
		{
			conn[0].fd = udpsock!=SOCKET_ERROR ? udpsock : udpsockv6;
			conn[0].events = POLLIN;
			conn[0].revents = 0;
			conn[1].revents = 0;
		}
		else
		{
			conn[0].fd = udpsock;
			conn[0].events = POLLIN;
			conn[0].revents = 0;
			conn[1].fd = udpsockv6;
			conn[1].events = POLLIN;
			conn[1].revents = 0;
		}
		rc = poll(conn, 1, 60*1000);
#endif
	}
	else
	{
		rc=SOCKET_ERROR;
	}

	if(rc>0)
	{
		for (size_t s = 0; s < 2; ++s)
		{
#ifdef _WIN32
			SOCKET curr_udpsock = s == 0 ? udpsock : udpsockv6;
			if (curr_udpsock == SOCKET_ERROR)
				continue;
			if (!FD_ISSET(curr_udpsock, &fdset))
				continue;
#else
			if (conn[s].revents == 0)
				continue;
			SOCKET curr_udpsock = conn[s].fd;
#endif
			//Log("Receiving UDP packet...");
			char buffer[BUFFERSIZE];
			socklen_t addrsize;
			DualSockAddr sender = {};
			if (curr_udpsock == udpsock)
				addrsize = sizeof(sockaddr_in);
			else
				addrsize = sizeof(sockaddr_in6);
			_i32 err = recvfrom(curr_udpsock, buffer, BUFFERSIZE, 0, (sockaddr*)&sender, &addrsize);
			if (err == SOCKET_ERROR)
			{
#ifdef LOG_SERVER
#ifdef _WIN32
				int err = WSAGetLastError();
#endif
				Server->Log("Recvfrom error in CUDPThread::UdpStep", LL_ERROR);
#ifdef _WIN32
				Server->Log("Last error: " + convert(err), LL_ERROR);
#endif
#endif
				has_error = true;
				return false;
			}
			else if (err > 0)
			{
				if (buffer[0] == ID_PING)
				{
					unsigned int rsleep = Server->getRandomNumber() % 500;
					//Log("UDP: PING received... sending PONG. Delay="+convert(rsleep)+"ms", LL_DEBUG);
					Server->wait(rsleep);
					char *buffer = new char[mServername.size() + 2];
					buffer[0] = ID_PONG;
					buffer[1] = FILESERV_VERSION;
					memcpy(&buffer[2], mServername.c_str(), mServername.size());
					int rc = sendto(curr_udpsock, buffer, (int)mServername.size() + 2, 0, (sockaddr*)&sender, addrsize);
					if (rc == SOCKET_ERROR)
					{
						Log("Sending reply failed " + convert(rc), LL_DEBUG);
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
	}
	else if( rc==SOCKET_ERROR )
	{
#ifdef LOG_SERVER
			Server->Log("Select error in CUDPThread::UdpStep", LL_ERROR);
#ifdef _WIN32
			Server->Log("Last error: "+ convert((int)GetLastError()), LL_ERROR);
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
#ifdef _WIN32
	::shutdown(udpsock, SD_BOTH);
#else
	::shutdown(udpsock, SHUT_RDWR);
#endif
}