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

#include "../vld.h"
#include <stdlib.h>
#ifdef _WIN32
#define DLLEXPORT extern "C" __declspec (dllexport)
#else
#define DLLEXPORT extern "C"
#endif

#include <vector>

#define DEF_SERVER
#include "../Interface/Server.h"
#include "../Interface/Action.h"

#include "HTTPService.h"
#include "../stringtools.h"

#include "MIMEType.h"
#include "IndexFiles.h"
#include "HTTPClient.h"

IServer *Server;
CHTTPService* http_service=NULL;
std::vector<std::string> allowed_urls;

DLLEXPORT void LoadActions(IServer* pServer)
{
	Server=pServer;

	CHTTPClient::init_mutex();

	add_default_mimetypes();
	add_default_indexfiles();

	std::string root=Server->getServerParameter("http_root");
	if( root=="" )
		root=".";

	std::string proxy_server=Server->getServerParameter("proxy_server");
	std::string s_proxy_port=Server->getServerParameter("proxy_port");
	int proxy_port=80;
	if(!s_proxy_port.empty())
	{
		proxy_port=atoi(s_proxy_port.c_str());
	}

	int share_proxy_connections=0;
	if(Server->getServerParameter("share_proxy_connections")=="1")
		share_proxy_connections=1;

	http_service=new CHTTPService(root, proxy_server, proxy_port, share_proxy_connections);

	int port=80;
	
	std::string p=Server->getServerParameter("http_port");
	if(p!="" )
		port=atoi(p.c_str());
		
	std::string allow_file=Server->getServerParameter("allowed_urls");
	if(!allow_file.empty())
	{
		std::string data=getFile(allow_file);
		int lines=linecount(data);
		for(int i=0;i<lines;++i)
		{
			allowed_urls.push_back(getline(i, data));
		}
	}

	Server->Log("Starting HTTP-Server on port "+nconvert(port), LL_INFO);

	Server->StartCustomStreamService( http_service, "HTTP", (unsigned short)port, 1);
}

DLLEXPORT void UnloadActions(void)
{
	if(Server->getServerParameter("leak_check")=="true")
	{
		CHTTPClient::destroy_mutex();
	}
}
