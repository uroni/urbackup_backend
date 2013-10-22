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

#include "vld.h"

#include <map>
#include "WorkerThread.h"
#include "Client.h"
#include "Server.h"
#include "libfastcgi/fastcgi.hpp"
#include "SelectThread.h"
#include "stringtools.h"
#include "Interface/File.h"

//#define EXTENSIVE_DEBUGGING

extern std::deque<CClient*> client_queue;
extern IMutex* clients_mutex;
extern ICondition* clients_cond;

CWorkerThread::CWorkerThread(CSelectThread *pMaster)
{
	stop_mutex=Server->createMutex();
	stop_cond=Server->createCondition();
	Master=pMaster;
	keep_alive=true;
	run=true;
}

CWorkerThread::~CWorkerThread()
{
  shutdown();
  Server->destroy(stop_cond);
  Server->destroy(stop_mutex);
}

void CWorkerThread::shutdown(void)
{
    IScopedLock slock(stop_mutex);
    Server->Log("waiting for worker...");
    run=false;
    clients_cond->notify_all();
    stop_cond->wait(&slock);
    Server->Log("done.");
}  

void CWorkerThread::operator()()
{
	while(run)
	{
		size_t nq=0;
		IScopedLock lock(clients_mutex);

		while(client_queue.size()==0 )
		{
			clients_cond->wait(&lock);
			if(!run)
			{
			  IScopedLock slock(stop_mutex);
			  stop_cond->notify_one();
			  return;
			}
		}
		
		
		{
			while( client_queue.size()>0 )
			{
				char buffer[WT_BUFFERSIZE];
				CClient *client=client_queue[0];
				client_queue.erase(client_queue.begin());
				SOCKET s=client->getSocket();	

				lock.relock(NULL);

				_i32 rc=recv(s, buffer, WT_BUFFERSIZE, MSG_NOSIGNAL);

				if( rc<1 )
				{
					keep_alive=true;
					//Server->Log("Client disconnected", LL_INFO);
					Master->RemoveClient( client );
					lock.relock(clients_mutex);
				}
				else
				{
#ifdef EXTENSIVE_DEBUGGING
					std::string lbuf;
					for(_i32 i=0;i<rc;++i)
					{
						if( buffer[i]==0 )
							lbuf+='#';
						else
							lbuf+=buffer[i];
					}
					Server->Log("Incoming data: "+lbuf, LL_INFO);
#endif
					client->lock();
					try
					{
						client->getFCGIProtocolDriver()->process_input(buffer, rc);
					}catch(...)
					{
						client->unlock();
						Master->RemoveClient(client);
						lock.relock(clients_mutex);
						continue;
					}
					
					FCGIRequest* req=NULL;
					try
					{
						req=client->getFCGIProtocolDriver()->get_request();
					}catch(...)
					{
						client->unlock();
						Master->RemoveClient(client);
						lock.relock(clients_mutex);
						continue;
					}

					client->unlock();

					if( req!=NULL )
						client->addRequest(req);

					while( (req=client->getAndRemoveReadyRequest())!=NULL )
					{
						Server->addRequest();
						client->lock();
						ProcessRequest(client, req);
						client->unlock();
					}

					if( keep_alive==false )
					{
						keep_alive=true;
						//Server->Log("Client disconnected", LL_INFO);
						Master->RemoveClient( client );
					}
					else
					{
						client->setProcessing(false);
						Master->WakeUp();
					}

					lock.relock(clients_mutex);
				}
			}
		}
	}
	IScopedLock slock(stop_mutex);
	stop_cond->notify_one();
}

void CWorkerThread::ProcessRequest(CClient *client, FCGIRequest *req)
{
	if( req->keep_connection )
	{
		keep_alive=true;
	}
	else
	{
		keep_alive=false;
	}

	if( req->role != FCGIRequest::RESPONDER )
	{
		Server->Log("Role ist not Responder", LL_ERROR);
		return;
	}

	str_map GET,POST;

	str_nmap::iterator iter=req->params.find("QUERY_STRING");
	if( iter!=req->params.end() )
	{
		for(size_t i=0,size=iter->second.size();i<size;++i)
		{
			if( iter->second[i]=='+' )
				iter->second[i]=' ';
		}
		ParseParamStr(iter->second, &GET );			
		req->params.erase( iter );
	}
	
	std::string ct=req->params["CONTENT_TYPE"];
	std::string lct=ct;
	strlower(lct);
	bool postfile=false;
	POSTFILE_KEY pfkey;
	if(lct.find("multipart/form-data")==std::string::npos)
	{
		if( req->stdin_stream.size()>0 && req->stdin_stream.size()<1048576 )
		{
			for(size_t i=0,size=req->stdin_stream.size();i<size;++i)
			{
				if( req->stdin_stream[i]=='+' )
					req->stdin_stream[i]=' ';
			}
			ParseParamStr(req->stdin_stream, &POST );
		}
	}
	else
	{
		std::string boundary=getafter("boundary=",ct);
		pfkey=ParseMultipartData(req->stdin_stream, boundary);
	        req->params["POSTFILEKEY"]=nconvert(pfkey);
	        postfile=true;
	}

	str_map::iterator iter2=GET.find(L"a");

	if( iter2!=GET.end() )
	{
		int starttime=Server->getTimeMS();

		str_map::iterator iter3=GET.find(L"c");

		std::wstring context;
		if( iter3!=GET.end() )
			context=iter3->second;

		THREAD_ID tid=Server->Execute(iter2->second, context, GET, POST, req->params, req );

		if( tid==0 )
		{
			std::wstring error=L"Error: Unknown action ["+iter2->second+L"]";
			Server->Log(error, LL_WARNING);
			req->write("Content-type: text/html; charset=UTF-8\r\n\r\n"+wnarrow(error));
		}

		starttime=Server->getTimeMS()-starttime;
		//Server->Log("Execution Time: "+nconvert(starttime)+" ms - time="+nconvert(Server->getTimeMS() ), LL_INFO);
	}
	else
	{
		std::string error="Error: Parameter 'action' not given.";
		req->write("Content-type: text/html; charset=UTF-8\r\n\r\n"+error);
	}
	
	if(postfile)
	{
		Server->clearPostFiles(pfkey);
	}

	req->end_request(0, FCGIRequest::REQUEST_COMPLETE);
}

POSTFILE_KEY CWorkerThread::ParseMultipartData(const std::string &data, const std::string &boundary)
{
	std::string rboundary="--"+boundary;
	int state=0;
	std::string key;
	std::string value;
	std::string filename;
	std::string name;
	std::string filedata;
	std::string contenttype;
	size_t start;
	POSTFILE_KEY pfilekey=Server->getPostFileKey();
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
				else if(key=="CONTENT-TYPE")
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
				Server->addPostFile(pfilekey, name, SPostfile(memfile, widen(filename), widen(contenttype)) );
				state=0;
				rboundary.erase(rboundary.size()-2,2);
				i+=rboundary.size()+2;
				state=0;
			}
	    }
	}
    return pfilekey;
}