/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
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
#include <stdlib.h>
#include "HTTPClient.h"
#include "../Interface/Pipe.h"
#include "../Interface/Thread.h"
#include "../Interface/ThreadPool.h"
#include "../Interface/Server.h"
#include "../Interface/File.h"
#include "../stringtools.h"
#include "HTTPService.h"
#include "HTTPFile.h"
#include "HTTPAction.h"
#include "HTTPProxy.h"

extern CHTTPService* http_service;

const int HTTP_STATE_COMMAND=0;
const int HTTP_STATE_HEADER=1;
const int HTTP_STATE_READY=2;
const int HTTP_STATE_CONTENT=3;
const int HTTP_STATE_WAIT_FOR_THREAD=4;
const int HTTP_STATE_KEEPALIVE=5;
const int HTTP_STATE_DONE=6;

const int HTTP_MAX_KEEPALIVE=15000;

IMutex *CHTTPClient::share_mutex=NULL;
std::map<std::string, SShareProxy> CHTTPClient::shared_connections;
extern std::vector<std::string> allowed_urls;

void CHTTPClient::Init(THREAD_ID pTID, IPipe *pPipe, const std::string& pEndpoint)
{
	pipe=pPipe;
	do_quit=false;
	http_g_state=HTTP_STATE_COMMAND;
	http_state=0;
	request_num=0;
	request_ticket=ILLEGAL_THREADPOOL_TICKET;
	fileupload=false;
	endpoint=pEndpoint;
}

void CHTTPClient::init_mutex(void)
{
	share_mutex=Server->createMutex();
}

void CHTTPClient::destroy_mutex(void)
{
	Server->destroy(share_mutex);
}

void CHTTPClient::ReceivePackets(void)
{
	std::string data;
	size_t rc=pipe->Read(&data);
	if( rc>0 )
	{
		for(size_t i=0;i<rc;++i)
		{
			char ch=data[i];
			switch(http_g_state)
			{
			case HTTP_STATE_KEEPALIVE:
				reset();
				http_g_state=HTTP_STATE_COMMAND;
				//fallthrough
			case HTTP_STATE_COMMAND:
				processCommand(ch);
				break;
			case HTTP_STATE_HEADER:
				processHeader(ch);
				break;
			case HTTP_STATE_CONTENT:
				processContent(ch);
				break;
			}

			if(http_g_state==HTTP_STATE_READY )
			{
				if(	processRequest() )
				{
					http_g_state=HTTP_STATE_WAIT_FOR_THREAD;
				}
				else
					do_quit=true;

				break;
			}

		}
	}
	else
	{
		do_quit=true;
	}
}

bool CHTTPClient::Run(void)
{
	if( http_g_state==HTTP_STATE_WAIT_FOR_THREAD )
	{
		if( Server->getThreadPool()->isRunning(request_ticket)==false )
		{
			//Server->Log("Connection: "+http_params["CONNECTION"], LL_DEBUG);
			/*if( strlower(http_params["CONNECTION"])=="close" )
			{
				http_g_state=HTTP_STATE_DONE;
			}
			else
			{
				http_g_state=HTTP_STATE_KEEPALIVE;
				http_keepalive_start=Server->getTimeMS();
				//Server->Log("Keep-alive: "+http_params["KEEP-ALIVE"], LL_DEBUG);
				str_map::iterator iter=http_params.find("KEEP-ALIVE");
				if(iter==http_params.end())
				{
					http_keepalive_count=HTTP_MAX_KEEPALIVE;
				}
				else
				{
					http_keepalive_count=std::min(atoi(iter->second.c_str()), HTTP_MAX_KEEPALIVE );
				}
			}*/
			http_g_state=HTTP_STATE_DONE;
			if(fileupload)
			{
				Server->clearPostFiles(tid);
			}
			delete request_handler;
			request_handler=NULL;

			if(!http_service->getProxyServer().empty() && http_service->getShareProxyConnections()==1)
			{
				IScopedLock lock(share_mutex);
				std::map<std::string, SShareProxy>::iterator it=shared_connections.find(http_query);
				if(it!=shared_connections.end())
				{
					Server->destroy(it->second.notify_pipe);
					Server->destroy(it->second.timeout_pipe);
					delete it->second.proxy;
					shared_connections.erase(it);
				}
			}
		}
	}

	if( do_quit==true || http_g_state==HTTP_STATE_DONE)
	{
		if(http_service->getProxyServer().empty() || http_service->getShareProxyConnections()==0)
		{
		    WaitForRemove();
		}
		else
		{
			Server->wait(10);
			if(Server->getThreadPool()->isRunning(request_ticket) )
			{
				IScopedLock lock(share_mutex);
				std::map<std::string, SShareProxy>::iterator it=shared_connections.find(http_query);
				if(it!=shared_connections.end())
				{
					std::vector<IPipe*> timeoutpipes;
					size_t rc;
					do
					{
						IPipe *t;
						rc=it->second.timeout_pipe->Read((char*)&t, sizeof(IPipe*),0);
						if(rc>0)
							timeoutpipes.push_back(t);
					}
					while(rc>0);

					bool found=false;
					for(size_t i=0;i<timeoutpipes.size();++i)
					{
						if(timeoutpipes[i]==pipe)
						{
							found=true;
						}
						else
						{
							it->second.timeout_pipe->Write((char*)&timeoutpipes[i], sizeof(IPipe*));
						}
					}

					if(found)
					{
						return false;
					}
					else
					{
						return true;
					}
				}
				else
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		return false;
	}

	if( http_g_state==HTTP_STATE_KEEPALIVE )
	{
		if( Server->getTimeMS()-http_keepalive_start>=http_keepalive_count )
		{
			return false;
		}
	}

	return true;
}

void CHTTPClient::processCommand(char ch)
{
	switch(http_state)
	{
	case 0:
		if( ch!=' ' )
			http_state=1;
		else
			break;
	case 1:
		if( ch==' ' && !http_method.empty() )
			http_state=2;
		else if( ch!=' ' )
			http_method+=(char)toupper(ch);
		break;
	case 2:
		if( ch!=' ' && ch!='\r' && ch!='\n' )
		{
			tmp+=ch;
			break;
		}
		else if( (ch==' ' || ch=='\r' || ch=='\n' ) && !tmp.empty() )
		{
			if( tmp=="HTTP/1.0" )
				http_version=10;
			else if( tmp=="HTTP/1.1" )
				http_version=11;
			else
			{
				if( tmp.size()>http_query.size() )
					http_query=tmp;
			}
			tmp.clear();

			if( ch=='\r' || ch=='\n'  )
				http_state=4;
			else
				break;
		}
	case 3:
		if( ch=='\n' )
			http_state=4;
		else if( ch=='\r' )
			break;
	case 4:
		http_g_state=HTTP_STATE_HEADER;
		http_state=0;
		tmp.clear();
	}
}

void CHTTPClient::processHeader(char ch)
{
	switch(http_state)
	{
	case 0:
		if( ch=='\r' || ch=='\n' )
			http_state=1;
		else 
		{
			if( ch!=':' )
				http_header_key+=(char)toupper(ch);
			else
			{
				http_state=3;
			}
			break;
		}
	case 1:
		if( ch=='\n' )
			http_state=2;
		else
			break;
	case 2:
		if( http_method=="POST")
		{
			str_map::iterator iter=http_params.find("CONTENT-LENGTH");
			if( iter!=http_params.end() )
			{
				http_remaining_content=atoi(iter->second.c_str() );
				if( http_remaining_content>0 )
				{
					http_g_state=HTTP_STATE_CONTENT;
					break;
				}
			}
		}
		http_g_state=HTTP_STATE_READY;
		break;

	case 3:
		if( ch!=' ')
			http_state=4;
		else
			break;
	case 4:
		if( ch!='\r' && ch!='\n' )
		{
			tmp+=ch;
			break;
		}
		else
		{
			http_params.insert(std::pair<std::string, std::string>(http_header_key, tmp) );
			tmp.clear();
			http_header_key.clear();
			http_state=5;
		}
	case 5:
		if( ch=='\n' )
			http_state=0;
		else
			break;
	}
}

void CHTTPClient::processContent(char ch)
{
	if( http_remaining_content>0 )
	{
		http_content+=ch;
		--http_remaining_content;
	}
	if( http_remaining_content<=0 )
	{
		http_g_state=HTTP_STATE_READY;
		
		str_map::iterator iter=http_params.find("CONTENT-TYPE");
		if(iter!=http_params.end())
		{
			std::string ct=iter->second;
			strlower(ct);
			if(ct.find("multipart/form-data")!=std::string::npos)
			{
				std::string boundary=getafter("boundary=", iter->second);
				ParseMultipartData(http_content, boundary);
			}
		}
	}
}

std::vector<std::string> CHTTPClient::parseHTTPPath(std::string pPath)
{
	std::string tmp;
	std::vector<std::string> ret;
	for(size_t i=0;i<pPath.size();++i)
	{
		char ch=pPath[i];
		if( ch!='/' )
		{
			tmp+=ch;
		}
		else
		{
			if( tmp!="" )
			{
				ret.push_back(tmp);
				tmp.clear();
			}
		}
	}
	if( tmp!="" )
	{
		ret.push_back(tmp);
	}
	return ret;
}

void CHTTPClient::parseAction(std::string pQuery, std::string &pAction, std::string &pContext)
{
	std::string tmp;
	int cs=0;
	char ch=0;
	char lch;
	for(size_t i=0;i<pQuery.size();++i)
	{
		lch=ch;
		ch=pQuery[i];
		
		if( ch=='=' && lch=='a' && pAction.empty() )
			cs=1;
		else if( ch=='=' && lch=='c' && pContext.empty())
			cs=2;
		else if( cs==1 )
		{
			if( ch=='&' )
				cs=0;
			else
				pAction+=ch;
		}
		else if(cs==2 )
		{
			if( ch=='&' )
				cs=0;
			else
				pContext+=ch;
		}
	}
}


bool CHTTPClient::processRequest(void)
{
	//Server->Log("Parsing done... starting handling request_num: "+convert(request_num)+" "+convert(Server->getTimeMS()), LL_INFO);
	++request_num;
	if(!allowed_urls.empty())
	{
		bool found=false;
		for(size_t i=0;i<allowed_urls.size();++i)
		{	
			if(allowed_urls[i]==http_query)
			{
				found=true;
				break;
			}
		}
		if(found==false)
		{
			Server->Log("URL not allowed");
			pipe->Write("HTTP/1.1 403 Not Allowed\r\nContent-Type: text/html\r\nContent-Length: 46\r\n\r\nSorry. You're not allowed to access this file.");
			request_handler=NULL;
			request_ticket=ILLEGAL_THREADPOOL_TICKET;
			return false;			
		}
	}
	std::vector<std::string> path=parseHTTPPath(http_query);

	if(!http_service->getProxyServer().empty())
	{
		if(http_service->getShareProxyConnections()==1)
		{
			IScopedLock lock(share_mutex);

			std::map<std::string, SShareProxy>::iterator it=shared_connections.find(http_query);
			if(it!=shared_connections.end())
			{
				it->second.notify_pipe->Write((char*)&pipe, sizeof(IPipe*) );
				request_ticket=it->second.proxy_ticket;
				request_handler=NULL;
				return true;
			}

			SShareProxy sp;
			sp.notify_pipe=Server->createMemoryPipe();
			sp.timeout_pipe=Server->createMemoryPipe();
			sp.proxy=new CHTTPProxy(http_method, http_query, http_version, http_content, http_params, pipe, sp.notify_pipe, sp.timeout_pipe);
			sp.proxy_ticket=Server->getThreadPool()->execute(sp.proxy);
			request_ticket=sp.proxy_ticket;
			request_handler=NULL;

			shared_connections[http_query]=sp;
		}
		else
		{
			CHTTPProxy *proxy_handler=new CHTTPProxy(http_method, http_query, http_version, http_content, http_params, pipe, NULL, NULL);
			request_ticket=Server->getThreadPool()->execute(proxy_handler);
			request_handler=proxy_handler;
		}
		return true;
	}

	if( path.size()>0 || http_query=="/" )
	{
		std::string *pl;
		if(path.size()>0)
			pl=&path[path.size()-1];
		else
			pl=&http_query;

		std::string name;
		std::string context;
		size_t pstart;
		if( pl->size()>1 && (*pl)[0]=='x' && (*pl)[1]=='?' )
		{
			parseAction(*pl, name, context);
		}
		else if(pl->find(".")==std::string::npos && path.size()<=2 && (pstart=pl->find("?"))!=std::string::npos)
		{
			name=pl->substr(0, pstart);
			
			if( path.size()>1 )
				context=path[0];
		}
		else
		{
			std::string rp;
			for(size_t i=0;i<path.size();++i)
			{
				if( i+1>=path.size() )
				{
					size_t li=path[i].find_last_of('?');
					if(li!=std::string::npos)
					{
						path[i]=path[i].substr(0,li);
					}
				}
				if( path[i]!=".." && path[i]!="." )
					rp+="/"+path[i];
			}
			CHTTPFile *file_handler=new CHTTPFile(http_service->getRoot()+rp, pipe);
			request_ticket=Server->getThreadPool()->execute(file_handler);
			request_handler=file_handler;
			return true;
		}

		std::string gparams=getafter("?", *pl);

		pl=NULL;
		http_params["REMOTE_ADDR"]=endpoint;
		CHTTPAction *action_handler=new CHTTPAction(name,context,gparams, http_content, http_params, pipe);
		request_ticket=Server->getThreadPool()->execute(action_handler);
		request_handler=action_handler;
		return true;
	}
	else
		return false;
}

void CHTTPClient::reset(void)
{
	http_params.clear();
	http_method.clear();
	http_query.clear();
	http_content.clear();
	http_state=0;
	tmp.clear();
	http_header_key.clear();
	request_ticket=ILLEGAL_THREADPOOL_TICKET;
	fileupload=false;
}

void CHTTPClient::WaitForRemove(void)
{
	if(request_ticket!=ILLEGAL_THREADPOOL_TICKET)
	{
		std::vector<THREADPOOL_TICKET> tmp;
		tmp.push_back(request_ticket);
		Server->getThreadPool()->waitFor(tmp);
	}
}

void CHTTPClient::ParseMultipartData(const std::string &data, const std::string &boundary)
{
	std::string rboundary="--"+boundary;
	int state=0;
	std::string key;
	std::string value;
	std::string filename;
	std::string name;
	std::string contenttype;
	size_t start;
	POSTFILE_KEY pfilekey=Server->getPostFileKey();
	http_params["POSTFILEKEY"]=convert(pfilekey);
	for(size_t i=0;i<data.size();++i)
	{
	    switch(state)
	    {
		case 0:
		    if(next(data,i,rboundary))
		    {
				i+=rboundary.size()+1;
				state=2;
		    }
		    break;
		case 1:
		    if(data[i]=='\n' || data[i]=='\r' )
			{
				if(data[i]=='\n')
				{
					state=4;
					rboundary+="--";
					start=i+1;
				}
				else
					break;
			}
		    else
				state=1;
		case 2:
			if(data[i]!=':')
				key+=toupper(data[i]);
		    else
				state=3;
		    break;
		case 3:
		    if(data[i]!='\n' && data[i]!='\r' )
				value+=data[i];
		    else if(data[i]=='\n')
			{
				if(key=="CONTENT-DISPOSITION")
				{
					name=getbetween("name=\"","\"", value);
					filename=getbetween("filename=\"","\"", value);
				}
				else if(key=="CONTENT-TYPE" )
				{
					contenttype=value;
				}
				value.clear();
				key.clear();
				state=1;
			}
		    break;
		case 4:
			if(next(data,i,rboundary)==true)
			{
                IFile *memfile=Server->openMemoryFile();
				memfile->Write(data.substr(start,i-start-2) );
				memfile->Seek(0);
				Server->addPostFile(pfilekey, name, SPostfile(memfile, filename, contenttype) );
				fileupload=true;
				state=0;
				rboundary.erase(rboundary.size()-2,2);
				i+=rboundary.size()+2;
				state=0;
			}
	    }
	}
}

