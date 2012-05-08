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

#ifdef _WIN32
#include <winsock2.h>
#endif
#include "StreamPipe.h"
#ifndef _WIN32
#include <memory.h>
#endif
#include "Server.h"

CStreamPipe::CStreamPipe( SOCKET pSocket)
	: throttle_bps(0), curr_bytes(0), lastresettime(0), transfered_bytes(0)
{
	s=pSocket;
	has_error=false;
}

CStreamPipe::~CStreamPipe()
{
	closesocket(s);
}

size_t CStreamPipe::Read(char *buffer, size_t bsize, int timeoutms)
{
	doThrottle(bsize);

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
	if( rc>0 )
	{
		rc=recv(s, buffer, (int)bsize, MSG_NOSIGNAL);
		if(rc<=0)
		{
			has_error=true;
			return 0;
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

bool CStreamPipe::Write(const char *buffer, size_t bsize, int timeoutms)
{
	doThrottle(bsize);

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
	size_t written=0;

	if(rc>0 )
	{
		rc=send(s, buffer,(int)bsize, MSG_NOSIGNAL);
		if(rc>=0)
		{
			written+=rc;
			if( written<bsize )
			{
				return Write(buffer+written, bsize-written, timeoutms);
			}
		}
		else
		{
			has_error=true;
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

	if( rc!=SOCKET_ERROR)
		return true;
	else
	{
		has_error=true;
		return false;
	}
}

bool CStreamPipe::Write(const std::string &str, int timeoutms)
{
	return Write(&str[0], str.size(), timeoutms);
}

size_t CStreamPipe::Read(std::string *ret, int timeoutms)
{
	char buffer[2000];
	size_t l=Read(buffer, 2000, timeoutms);
	if( l>0 )
	{
		ret->resize(l);
		memcpy((char*)ret->c_str(), buffer, l);
	}
	else
	{
		return 0;
	}
	return l;
}

bool CStreamPipe::isWritable(int timeoutms)
{
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(s, &fdset);

	timeval *tv=NULL;
	timeval to;
	if( timeoutms>=0 )
	{
		to.tv_sec=(long)(timeoutms/1000);
		to.tv_usec=(long)(timeoutms%1000)*1000;
		tv=&to;
	}

	int rc=select((int)s+1, 0, &fdset, 0, tv);
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
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(s, &fdset);

	timeval *tv=NULL;
	timeval to;
	if( timeoutms>=0 )
	{
		to.tv_sec=(long)(timeoutms/1000);
		to.tv_usec=(long)(timeoutms%1000)*1000;
		tv=&to;
	}

	int rc=select((int)s+1, &fdset, 0, 0, tv);
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

void CStreamPipe::setThrottle(size_t bps)
{
	throttle_bps=bps;
}

void CStreamPipe::doThrottle(size_t new_bytes)
{
	transfered_bytes+=new_bytes;

	if(throttle_bps==0) return;

	unsigned int ctime=Server->getTimeMS();

	if(ctime-lastresettime>10000)
	{
		lastresettime=ctime;
		curr_bytes=0;
	}

	curr_bytes+=new_bytes;

	unsigned int passed_time=ctime-lastresettime;
	if(passed_time>0)
	{
		size_t bps=(curr_bytes*1000)/passed_time;
		if(bps>throttle_bps)
		{
			size_t maxRateTime=(curr_bytes*1000)/throttle_bps;
			unsigned int sleepTime=(unsigned int)(maxRateTime-passed_time);

			if(sleepTime>9)
			{
				Server->wait(sleepTime);

				if(passed_time>1000)
				{
					curr_bytes=0;
					lastresettime=Server->getTimeMS();
				}
			}
		}
	}
}

size_t CStreamPipe::getTransferedBytes(void)
{
	return transfered_bytes;
}

void CStreamPipe::resetTransferedBytes(void)
{
	transfered_bytes=0;
}