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

#include "vld.h"

#include "Client.h"
#include "Server.h"
#include "AcceptThread.h"
#include "libfastcgi/fastcgi.hpp"

CClient::CClient()
{
	mutex=Server->createMutex();
	m_lock=NULL;
	processing=false;
}

CClient::~CClient()
{
	Server->destroy(mutex);
}

SOCKET CClient::getSocket()
{
	return s;
}

OutputCallback * CClient::getOutputCallback()
{
	return output;
}

FCGIProtocolDriver * CClient::getFCGIProtocolDriver()
{
	return driver;
}

#ifndef TCP_CORK
#define TCP_CORK TCP_NOPUSH
#endif

void CClient::set(SOCKET ps, OutputCallback *poutput, FCGIProtocolDriver * pdriver )
{
	IScopedLock l(mutex);
	s=ps;
	output=poutput;
	driver=pdriver;

	int flag;
#ifdef _WIN32
	flag=1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
#else
	flag=0;
	setsockopt(s, IPPROTO_TCP, TCP_CORK, (char *) &flag, sizeof(int));
#endif
}

void  CClient::lock()
{
	IScopedLock *n_lock=new IScopedLock(mutex);
	m_lock=n_lock;
}

void  CClient::unlock()
{
	delete m_lock;
}

void CClient::remove()
{
	IScopedLock l(mutex);
	delete output;
	delete driver;
}

void CClient::addRequest( FCGIRequest* req)
{
	IScopedLock l(mutex);
	requests.push_back( req );
}

bool CClient::removeRequest( FCGIRequest *req)
{
	IScopedLock l(mutex);
	for(size_t i=0;i<requests.size();++i)
	{
		if(requests[i]==req )
		{
			requests.erase( requests.begin()+i);
			return true;
		}
	}
	return false;
}

size_t CClient::numRequests(void)
{
	IScopedLock l(mutex);
	return requests.size();
}

FCGIRequest* CClient::getRequest(size_t num)
{
	IScopedLock l(mutex);
	if( num>=requests.size() )
		return NULL;
	return requests[num];
}

FCGIRequest* CClient::getAndRemoveReadyRequest(void)
{
	IScopedLock l(mutex);
	for(size_t i=0;i<requests.size();++i)
	{
		if( requests[i]->stdin_eof==true )
		{
			FCGIRequest* req=requests[i];
			requests.erase( requests.begin()+i);
			return req;
		}
	}
	return NULL;
}

bool CClient::isProcessing(void)
{
	return processing;
}

bool CClient::setProcessing(bool b)
{
	IScopedLock l(mutex);
	bool ret=processing;
	processing=b;
	return ret;
}
