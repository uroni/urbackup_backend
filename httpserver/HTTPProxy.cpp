/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include <memory.h>
#include "HTTPProxy.h"
#include "HTTPService.h"
#include "../Interface/Server.h"
#include "../Interface/Pipe.h"
#include "../stringtools.h"

extern CHTTPService* http_service;

CHTTPProxy::CHTTPProxy(std::string pHttp_method, std::string pHttp_query, int pHttp_version, const std::string pPOSTStr, const str_nmap &pRawPARAMS, IPipe *pOutput, IPipe *pNotify, IPipe *pTimeoutPipe) :
http_method(pHttp_method), http_query(pHttp_query), http_version(pHttp_version), POSTStr(pPOSTStr), RawPARAMS(pRawPARAMS), notify(pNotify), timeoutpipe(pTimeoutPipe)
{
	output.push_back(pOutput);
	sync.push_back(2);
	output_buffer.push_back(std::queue<CBuffer>());
	timeouts.push_back(Server->getTimeMS());
}

void CHTTPProxy::operator()(void)
{
	std::string html_ver=" HTTP/1.1";
	if(http_version==10)html_ver=" HTTP/1.0";

	std::string request=http_method+" "+http_query+html_ver+"\r\n";
	request+="Host: "+http_service->getProxyServer()+":"+nconvert(http_service->getProxyPort())+"\r\n";
	for(str_nmap::iterator it=RawPARAMS.begin();it!=RawPARAMS.end();++it)
	{
		if(strlower(it->first)!="host")
		{
			if(it->first.size()>0)
			{
				std::string c=it->first.substr(0,1);
				strupper(&c);
				request+=c;
				request+=strlower(it->first.substr(1,it->first.size()-1))+": "+it->second+"\r\n";
			}
		}
	}
	if(!POSTStr.empty())
	{
		request+="\r\n"+POSTStr;
	}
	else
	{
		request+="\r\n";
	}

	IPipe *srv=Server->ConnectStream(http_service->getProxyServer(), http_service->getProxyPort(), 30000);
	if(srv==NULL)
	{
		for(size_t i=0;i<output.size();++i)
		{
			output[i]->Write("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 33\r\n\r\nSorry. Could not connect to host.");
		}
		return;
	}
	
	Server->Log("Starting streaming for url: \""+http_query+"\"");

	srv->Write(request);

	char *buf=new char[1500];
	unsigned int nbuffers=0;
	size_t rc;
	while((rc=srv->Read(buf,1500, 30000))>0 )
	{
		CBuffer nb(buf, rc);
		nb.rcount=new int;
		++nbuffers;
		*(nb.rcount)=1;
		if(notify!=NULL)
		{
			IPipe *np;
			if(notify->Read((char*)&np, sizeof(IPipe*), 0))
			{
				Server->Log("New streaming client for url: \""+http_query+"\" "+nconvert(output.size())+" streaming clients.");
				output_buffer.push_back(std::queue<CBuffer>());
				output.push_back(np);
				sync.push_back(0);
				timeouts.push_back(Server->getTimeMS());
			}
		}
		bool non_sync=false;
		for(size_t i=0;i<output.size();++i)
		{
			if(sync[i]==2)
			{
				output_buffer[i].push(nb);
				++(*nb.rcount);
			}
			else
			{
				non_sync=true;
			}
		}
		if(non_sync)
		{
			for(size_t i=0;i<rc;++i)
			{
				if(buf[i]==0x47 && i+188<rc && buf[i+188]==0x47)
				{
					for(size_t j=0;j<output.size();++j)
					{
						if(sync[j]!=2)
						{
							if(sync[j]==0)
							{
								const char *vv="HTTP/1.0 200 OK\r\nContent-Type: video/mpeg\r\n\r\n";
								size_t vv_len=strlen(vv);
								char *msg=new char[vv_len+1];
								memcpy(msg, vv, vv_len);
								CBuffer b(msg, vv_len);
								b.rcount=new int;
								*b.rcount=1;
								++nbuffers;
								output_buffer[j].push(b);
								Server->Log("Synced client");
							}

							nb.offset=(int)i;
							output_buffer[j].push(nb );
							++(*nb.rcount);
							nb.offset=0;
							sync[j]=2;
						}
					}
				}
			}
		}
		for(size_t i=0;i<output.size();++i)
		{
			while(sync[i]==2 && !output_buffer[i].empty())
			{
				if( output[i]->Write(output_buffer[i].front().buf+output_buffer[i].front().offset, output_buffer[i].front().bsize-output_buffer[i].front().offset,0) )
				{
					timeouts[i]=Server->getTimeMS();
					--(*output_buffer[i].front().rcount);
					if((*output_buffer[i].front().rcount)<=0)
					{
						delete output_buffer[i].front().rcount;
						delete [] output_buffer[i].front().buf;
						--nbuffers;
					}
					output_buffer[i].pop();
				}
				else
				{
					break;
				}
			}
			if(output_buffer[i].size()>100000)
			{
				Server->Log("Buffer overflow for client. Emptying buffer and resyncing.");
				while(!output_buffer[i].empty())
				{
					--(*output_buffer[i].front().rcount);
					if((*output_buffer[i].front().rcount)<=0)
					{
						delete output_buffer[i].front().rcount;
						delete [] output_buffer[i].front().buf;
						--nbuffers;
					}
					output_buffer[i].pop();
				}
				sync[i]=1;
			}
		}

		bool c=true;
		while(c)
		{
			c=false;
			for(size_t i=0;i<timeouts.size();++i)
			{
				if(Server->getTimeMS()-timeouts[i]>10000)
				{
					Server->Log("Client timeout");
					c=true;
					while(!output_buffer[i].empty())
					{
						--(*output_buffer[i].front().rcount);
						if( (*output_buffer[i].front().rcount)<=0)
						{
							delete output_buffer[i].front().rcount;
							delete [] output_buffer[i].front().buf;
							--nbuffers;
						}
						output_buffer[i].pop();
					}
					output_buffer.erase(output_buffer.begin()+i);

					output[i]->shutdown();
					timeoutpipe->Write((char*)&output[i], sizeof(IPipe*));

					output.erase(output.begin()+i);
					sync.erase(sync.begin()+i);
					timeouts.erase(timeouts.begin()+i);
					break;
				}
			}
		}
		--(*nb.rcount);
		if(*nb.rcount<=0)
		{
			delete nb.rcount;
			delete [] nb.buf;
			--nbuffers;
		}
		if(output.empty())
		{
			Server->Log("No streaming clients left. nbuffers="+nconvert(nbuffers));
			break;
		}
		buf=new char[1500];
	}
	if(rc==0)
	{
		Server->Log("Server closed connection.");
	}
	Server->Log("Streaming done.");
}