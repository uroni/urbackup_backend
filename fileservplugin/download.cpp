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

#include "settings.h"
#ifdef CAMPUS

#ifdef _WIN32
	#include <windows.h>
	#define MSG_NOSIGNAL 0
#else
	#include <signal.h>
	#include <sys/socket.h>
	#include <sys/types.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <memory.h>
	#define SOCKET_ERROR -1
	#define closesocket close
#endif

#include "download.h"

#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include "../stringtools.h"


using namespace std;

bool downloadwidthref=false;
string lastdl;

string proxy="";
int proxyport=8080;
bool http1_1=true;
bool stats=false;
bool fullrequest=true;

const int buffersize=1024;

timeval timeout={ 60 ,0 };

string getafter(char ch,string str)
{
        unsigned int offset=(unsigned int)str.find(ch);
        offset+=1;
        return str.substr(offset);
}

in_addr getIP(string ip)
{
    const char* host=ip.c_str();
    in_addr dest;
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE)
	{
        dest.s_addr = addr;
		return dest;
    }
    else
	{
        hostent* hp = gethostbyname(host);
        if (hp != 0)
		{
            memcpy(&(dest), hp->h_addr, hp->h_length);
        }
        else
		{
			memset(&dest,0,sizeof(in_addr) );
            return dest;
        }
    }
    return dest;
}


string deencchunked(string ret)
{
	size_t off=ret.find("\r\n\r\n");
	if(ret.find("Transfer-Encoding: chunked")<off)
	{
			string nret;
			nret+=getuntilinc("\r\n\r\n",ret);
			int pos=(int)ret.find("\r\n\r\n")+4;
			while(true)
			{
				string sub=ret.substr(pos,20);
				string hex=getuntil("\r\n",sub);
				if(hex=="0")
					break;
				if( IsHex(hex)==false)
				{
					nret+=ret.substr(pos,ret.size()-pos);
					return nret;
				}
				unsigned long h=hexToULong((char*)hex.c_str() );
				int sstartpos=(int)(pos+hex.size()+2);
				if(sstartpos+h>=ret.size() )
				{
					return nret;
				}
				nret+=ret.substr(sstartpos,h);
				pos+=(int)hex.size()+2+h+2;
			}
			return nret;
	}
	return "";
}

string download(string url,string ref,string cookie)
{
        int Cs;
        string ret;
        sockaddr_in addr;

        //PARSE
        //http://www.xxx.de/index.htm
        string host;
        string site;
        int pos=0;
        //--------
        for(unsigned int i=0;i<url.length();i++)
        {
                if(url[i]=='/'){
                        pos++;
                        if(pos<3)continue;
                }
                if(pos==2)host+=url[i];
                if(pos>2) site+=url[i];
        }


#ifdef _WIN32
        WSADATA wsa;
	int rc = WSAStartup(MAKEWORD(2, 0), &wsa);
	if(rc == SOCKET_ERROR)
	{
		cout << "Error: WSAStartup" << endl;
		return "";
	}
#endif

        Cs=(int)socket(AF_INET,SOCK_STREAM,0);

        addr.sin_family=AF_INET;

		if(proxy=="")
		{
			addr.sin_port=htons(80);
			addr.sin_addr=getIP(host);
		}
		else
		{
			addr.sin_port=htons(proxyport);
			addr.sin_addr=getIP(proxy);
		}


#ifdef _WIN32
		u_long nonBlocking;

		// auf nonblocking setzen:
		nonBlocking=1;
		ioctlsocket(Cs,FIONBIO,&nonBlocking);
#else
		fcntl(Cs,F_SETFL,O_NONBLOCK);
#endif


        int error;

        connect(Cs,(sockaddr*)&addr,sizeof(sockaddr));
       

		fd_set conn;
		FD_ZERO(&conn);
		FD_SET(Cs, &conn);

		if(timeout.tv_sec==0)
		{
			timeout.tv_sec=60;
			timeout.tv_usec=0;
		}

		error=select(Cs+1,NULL,&conn,NULL,&timeout);

		if(error<1)
		{
			return "";
		}

		string tosend;
        if(http1_1==true)
		{
			tosend="GET "+site+" HTTP/1.1\r\n";
					
		}
		else
		{
			tosend="GET "+url+" HTTP/1.0\r\n";
		}
		tosend=tosend+  "Host: "+host+"\r\n"+
                        "User-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7) Gecko/20040626 Firefox/0.9.1\r\n"+
                        "Accept: text/xml,application/xml,application/xhtml+xml,text/html;q=0.9,text/plain;q=0.8,video/x-mng,image/png,image/jpeg,image/gif;q=0.2,*/*;q=0.1\r\n"+
                        "Accept-Language: de-at,de;q=0.8,en-us;q=0.5,en;q=0.3\r\n"+
                        "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n"+
                        "Connection: close\r\n";
		if(cookie!="")
		{
			tosend+="Cookie: "+cookie+"\r\n";
		}
		if(downloadwidthref==true&&ref!="")
		{
                        tosend+="Referer: "+ref+"\r\n\r\n";
		}
		else if(downloadwidthref==true&&lastdl!="")
		{
						tosend+="Referer: "+lastdl+"\r\n\r\n";
		}
		else
		{
			tosend+="\r\n";
		}

        send(Cs,tosend.c_str(),(int)tosend.length(), MSG_NOSIGNAL);

		select(Cs+1,NULL,&conn,NULL,&timeout);

		char *Buffer=new char[buffersize+1];

        int bytes=buffersize;
        while(bytes>0)
        {                
				error=select(Cs+1,&conn,NULL,NULL,&timeout);
				if(error>0)
				{
					bytes = recv(Cs, Buffer, buffersize,  MSG_NOSIGNAL);
					Buffer[bytes]='\0';
					ret+=Buffer;
				}
				else
					bytes=0; 
        }

		delete[] Buffer;

		closesocket(Cs);

		lastdl=url;

		string nret=deencchunked(ret);
		if(nret!="")
		{
			ret=nret;
		}

		return ret;
}

#endif //CAMPUS
