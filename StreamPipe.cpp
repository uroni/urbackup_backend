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

#ifdef _WIN32
#include <winsock2.h>
#endif
#include "StreamPipe.h"
#ifndef _WIN32
#include <memory.h>
#include <errno.h>
#endif
#include "Server.h"
#include "Interface/PipeThrottler.h"
#include "stringtools.h"

std::map<CStreamPipe*, std::string> CStreamPipe::active_pipes;
IMutex* CStreamPipe::active_pipes_mutex = NULL;

CStreamPipe::CStreamPipe( SOCKET pSocket, const std::string& usage_str)
	: transfered_bytes(0)
{
#ifdef _WIN32
	socketEvt = WSA_INVALID_EVENT;
#else
	wakeup[0] = -1;
#endif

	s=pSocket;
	has_error=false;
	IScopedLock lock(active_pipes_mutex);
	active_pipes[this] = usage_str;
}

CStreamPipe::~CStreamPipe()
{
	closesocket(s);

	IScopedLock lock(active_pipes_mutex);
	std::map<CStreamPipe*, std::string>::iterator it = active_pipes.find(this);
	if (it != active_pipes.end())
		active_pipes.erase(it);
}

namespace
{
	int selectSocketRead(SOCKET s, int timeoutms)
	{
#ifdef _WIN32
		fd_set conn;
		FD_ZERO(&conn);
		FD_SET(s, &conn);

		timeval *tv=NULL;
		timeval to;
		if( timeoutms>=0 )
		{
			to.tv_sec=(long)(timeoutms/1000);
			to.tv_usec=(long)(timeoutms%1000)*1000;
			tv=&to;
		}

		int rc=select((int)s+1,&conn,NULL,NULL,tv);
#else
		pollfd conn[1];
		conn[0].fd=s;
		conn[0].events=POLLIN;
		conn[0].revents=0;
		int rc = poll(conn, 1, timeoutms);
#endif
		return rc;
	}

	int selectSocketReadWrite(SOCKET s, int timeoutms)
	{
#ifdef _WIN32
		fd_set conn;
		FD_ZERO(&conn);
		FD_SET(s, &conn);

		timeval *tv = NULL;
		timeval to;
		if (timeoutms >= 0)
		{
			to.tv_sec = (long)(timeoutms / 1000);
			to.tv_usec = (long)(timeoutms % 1000) * 1000;
			tv = &to;
		}

		int rc = select((int)s + 1, &conn, &conn, NULL, tv);
#else
		pollfd conn[1];
		conn[0].fd = s;
		conn[0].events = POLLIN|POLLOUT;
		conn[0].revents = 0;
		int rc = poll(conn, 1, timeoutms);
#endif
		return rc;
	}

	int selectSocketWrite(SOCKET s, int timeoutms)
	{
#ifdef _WIN32
		fd_set conn;
		FD_ZERO(&conn);
		FD_SET(s, &conn);

		timeval *tv=NULL;
		timeval to;
		if( timeoutms>=0 )
		{
			to.tv_sec=(long)(timeoutms/1000);
			to.tv_usec=(long)(timeoutms%1000)*1000;
			tv=&to;
		}

		int rc=select((int)s+1,NULL,&conn,NULL,tv);
#else
		pollfd conn[1];
		conn[0].fd=s;
		conn[0].events=POLLOUT;
		conn[0].revents=0;
		int rc = poll(conn, 1, timeoutms);
#endif
		return rc;
	}
}

size_t CStreamPipe::Read(char *buffer, size_t bsize, int timeoutms)
{
	int rc = selectSocketRead(s, timeoutms);

	if( rc>0 )
	{
		rc=recv(s, buffer, (int)bsize, MSG_NOSIGNAL);
		if(rc<=0)
		{
#ifdef _WIN32
			DWORD err = WSAGetLastError();
			if(err!=WSAEWOULDBLOCK && err!=WSAEINTR)
			{
				has_error=true;
			}
#else
			if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)
			{
				has_error=true;
			}
#endif
			return 0;
		}
		else
		{
			doThrottle(rc, false, true);
		}
	}
	if( rc>0 )
		return rc;
	else
	{
		if(rc<0)
		{
		    has_error=true;
		}
		return 0;
	}
}

bool CStreamPipe::Write(const char *buffer, size_t bsize, int timeoutms, bool flush)
{
	int rc = selectSocketWrite(s, timeoutms);
	size_t written=0;

	if(rc>0 )
	{
		rc=send(s, buffer,(int)bsize, MSG_NOSIGNAL);
		if(rc>=0)
		{
			doThrottle(rc, true, true);

			written+=rc;
			if( written<bsize )
			{
				return Write(buffer+written, bsize-written, -1, flush);
			}
		}
		else
		{
#ifdef _WIN32
			DWORD err = WSAGetLastError();
			if(err==EINTR)
			{
				return Write(buffer, bsize, timeoutms, flush);
			}

			if(err!=WSAEWOULDBLOCK)
			{
				has_error = true;
			}
#else
			if(errno==EINTR)
			{
				return Write(buffer, bsize, timeoutms, flush);
			}

			if(errno!=EAGAIN && errno!=EWOULDBLOCK)
			{
				has_error=true;
			}
#endif
			return false;
		}
	}
	else
	{
		if(rc<0)
		{
			has_error=true;
		}
		return false;
	}

	return true;
}

bool CStreamPipe::Write(const std::string &str, int timeoutms, bool flush)
{
	return Write(&str[0], str.size(), timeoutms, flush);
}

size_t CStreamPipe::Read(std::string *ret, int timeoutms)
{
	char buffer[8192];
	size_t l=Read(buffer, 8192, timeoutms);
	if( l>0 )
	{
		ret->assign(buffer, l);
	}
	else
	{
		return 0;
	}
	return l;
}

bool CStreamPipe::isWritable(int timeoutms)
{
	if(!doThrottle(0, true, false))
	{
		return false;
	}

	int rc = selectSocketWrite(s, timeoutms);
	if( rc>0 )
		return true;
	else
	{
		if(rc<0)
		{
			has_error=true;
		}
		return false;
	}
}

bool CStreamPipe::isReadable(int timeoutms)
{
	if(!doThrottle(0, false, false))
	{
		return false;
	}

	if (hasWakeup())
		return isReadableWithWakeup(timeoutms);

	int rc = selectSocketRead(s, timeoutms);
	if( rc>0 )
		return true;
	else
	{
		if(rc<0)
		{
			has_error=true;
		}
		return false;
	}
}

bool CStreamPipe::isReadOrWritable(int timeoutms)
{
	if (!doThrottle(0, false, false))
	{
		return false;
	}

	if (!doThrottle(0, true, false))
	{
		return false;
	}

	int rc = selectSocketReadWrite(s, timeoutms);
	if (rc>0)
		return true;
	else
	{
		if (rc<0)
		{
			has_error = true;
		}
		return false;
	}
}

bool CStreamPipe::hasError(void)
{
	return has_error;
}

SOCKET CStreamPipe::getSocket(void)
{
	return s;
}

void CStreamPipe::shutdown(void)
{
#ifdef _WIN32
	::shutdown(s, SD_BOTH);
#else
	::shutdown(s, SHUT_RDWR);
#endif
}

bool CStreamPipe::doThrottle(size_t new_bytes, bool outgoing, bool wait)
{
	transfered_bytes+=new_bytes;

	if(outgoing)
	{
		bool b=true;
		for(size_t i=0;i<outgoing_throttlers.size();++i)
		{
			b = b && outgoing_throttlers[i]->addBytes(new_bytes, wait);
		}
		return b;
	}
	else
	{
		bool b=true;
		for(size_t i=0;i<incoming_throttlers.size();++i)
		{
			b = b && incoming_throttlers[i]->addBytes(new_bytes, wait);
		}
		return b;
	}
}

void CStreamPipe::init_mutex()
{
	active_pipes_mutex = Server->createMutex();
}

void CStreamPipe::setUsageString(const std::string& str)
{
	IScopedLock lock(active_pipes_mutex);
	active_pipes[this] = str;
}

std::vector<std::string> CStreamPipe::getPipeList()
{
	std::vector<std::string> ret;
	IScopedLock lock(active_pipes_mutex);
	for (std::map<CStreamPipe*, std::string>::iterator it =active_pipes.begin();
		it!=active_pipes.end();++it)
		ret.push_back(it->second);

	return ret;
}

bool CStreamPipe::setCompressionSettings(const SCompressionSettings& params)
{
	return false;
}

bool CStreamPipe::setOption(const SocketOption opt)
{
	switch (opt)
	{
	case SocketOption_NoDelay:
		int flag;
		flag = 1;
		return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(int)) == 0;
	case SocketOption_CanWakeup:
		return initWakeup();
	}
	return false;
}

bool CStreamPipe::wakeupRead()
{
	if (!hasWakeup())
		return false;
#ifdef _WIN32
	return SetEvent(wakeupEvt) != FALSE;
#else
	char ch = 1;
	return write(pipe[1], &ch, 1) == 1;
#endif
}

_i64 CStreamPipe::getTransferedBytes(void)
{
	return transfered_bytes;
}

void CStreamPipe::resetTransferedBytes(void)
{
	transfered_bytes=0;
}

void CStreamPipe::addThrottler(IPipeThrottler *throttler)
{
	if (throttler != NULL)
	{
		incoming_throttlers.push_back(throttler);
		outgoing_throttlers.push_back(throttler);
	}
}

void CStreamPipe::addOutgoingThrottler(IPipeThrottler *throttler)
{
	if (throttler != NULL)
	{
		outgoing_throttlers.push_back(throttler);
	}
}

void CStreamPipe::addIncomingThrottler(IPipeThrottler *throttler)
{
	if (throttler != NULL)
	{
		incoming_throttlers.push_back(throttler);
	}
}

bool CStreamPipe::Flush( int timeoutms/*=-1 */ )
{
	return true;
}

bool CStreamPipe::initWakeup()
{
#ifdef _WIN32
	socketEvt = WSACreateEvent();
	if (socketEvt == WSA_INVALID_EVENT)
	{
		Server->Log("Error creating WSA socket event: " + convert(WSAGetLastError()), LL_ERROR);
		return false;
	}

	wakeupEvt = WSACreateEvent();
	if (wakeupEvt == WSA_INVALID_EVENT)
	{
		Server->Log("Error creating WSA socket event: " + convert(WSAGetLastError()), LL_ERROR);
		return false;
	}

	const int rc = WSAEventSelect(s, socketEvt, FD_READ | FD_CLOSE);
	if (rc == SOCKET_ERROR)
	{
		Server->Log("Error setting event select: " + convert(WSAGetLastError()), LL_ERROR);
		return false;
	}
#else
	if (clopipe(wakeup) == -1)
	{
		Server->Log("Error creating wakeup pipe: " + convert(errno), LL_ERROR);
		wakeup[0] = -1;
		return false;
	}
#endif

	return true;
}

bool CStreamPipe::hasWakeup() const
{
#ifdef _WIN32
	return socketEvt != WSA_INVALID_EVENT;
#else
	return wakeup[0] != -1;
#endif
}

bool CStreamPipe::isReadableWithWakeup(int timeoutms)
{
#ifdef _WIN32
	WSAEVENT eventArray[2];
	eventArray[0] = wakeupEvt;
	eventArray[1] = socketEvt;

	const DWORD rc = WSAWaitForMultipleEvents(2, eventArray, FALSE, timeoutms>=0 ? timeoutms : WSA_INFINITE, FALSE);

	if (rc == WSA_WAIT_FAILED)
	{
		has_error = true;
		return false;
	}
	else if (rc != WSA_WAIT_TIMEOUT)
	{
		if (rc == WSA_WAIT_EVENT_0)
		{
			ResetEvent(wakeupEvt);
		}

		WSANETWORKEVENTS networkEvents;
		if (WSAEnumNetworkEvents(s, socketEvt, &networkEvents) == 0
			&& networkEvents.lNetworkEvents > 0)
		{
			return true;
		}
	}

	return false;
#else
	pollfd conn[2];

	conn[0].fd = wakeup[0];
	conn[0].events = POLLIN;
	conn[0].revents = 0;

	conn[1].fd = s;
	conn[1].events = POLLIN;
	conn[1].revents = 0;

	int rc = poll(conn, 2, timeoutms);

	if (rc < 0)
	{
		has_error = true;
		return false;
	}
	else if(rc>0)
	{
		if (conn[1].revents != 0)
			return true;

		return false;
	}

	return false;
#endif
}