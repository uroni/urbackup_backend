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

#include "../vld.h"
#include "HTTPService.h"
#include "HTTPClient.h"

CHTTPService::CHTTPService(std::string pRoot, std::string pProxy_server, int pProxy_port, int pShare_proxy_connections)
{
	root=pRoot;
	proxy_server=pProxy_server;
	proxy_port=pProxy_port;
	share_proxy_connections=pShare_proxy_connections;
}

ICustomClient* CHTTPService::createClient()
{
	return new CHTTPClient();
}

void CHTTPService::destroyClient( ICustomClient * pClient)
{
	CHTTPClient *c=(CHTTPClient*)pClient;
	delete c;
}

std::string CHTTPService::getRoot(void)
{
	return root;
}

std::string CHTTPService::getProxyServer(void)
{
	return proxy_server;
}

int CHTTPService::getProxyPort(void)
{
	return proxy_port;
}

int CHTTPService::getShareProxyConnections(void)
{
	return share_proxy_connections;
}