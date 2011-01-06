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
#else
	#include <unistd.h>
	#include <netdb.h>
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <ifaddrs.h>
#endif

#include "CampusThread.h"
#include "download.h"
#include "../stringtools.h"
#include "socket_header.h"

#ifdef LINUX
extern bool c_run;
#endif

void TSleep(int sec)
{
#ifdef _WIN32
	Sleep(sec*1000);
#else
	sleep(sec);
#endif
}

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";


static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

bool ReadReg(void)
{
#ifndef _WIN32
	return true;
#else
	HKEY hkey; 
	LONG ret=RegCreateKeyExA(HKEY_LOCAL_MACHINE,"SOFTWARE\\UrInstaller\\settings",0,0,REG_OPTION_NON_VOLATILE,KEY_QUERY_VALUE,NULL,&hkey,0);

	if( ret==ERROR_SUCCESS)
	{
		DWORD dwRet=-1;
		DWORD dwSize=sizeof(DWORD);
		LONG ret=RegQueryValueExA(hkey,"InternetSearch",NULL, NULL, (LPBYTE)&dwRet, &dwSize ); 

		if( ret!=ERROR_SUCCESS || dwRet==1 )
		{
			RegCloseKey(hkey);
			return true;
		}
		else
		{
			RegCloseKey(hkey);
			return false;
		}
	}
	return true;
#endif
}

CCampusThread::CCampusThread(std::string sname)
{
	servername=base64_encode((const unsigned char*)sname.c_str(), (unsigned int)sname.size());
	for(size_t i=0;i<servername.size();++i)
	{
		if( servername[i]=='=')
		{
			servername.erase(i,1);
			servername.insert(i, "%3D");
		}
	}
}

void CCampusThread::operator()(void)
{
	if( ReadReg()==true )
	{
		while(true)
		{
			std::string ips;
			{
#ifdef _WIN32
				char hostname[1024];
				struct    hostent* h;

				int rc=gethostname(hostname, 1023);
				if(rc!=-1)
				{
					if(NULL != (h = gethostbyname(hostname)))
					{
							for(unsigned int x = 0; (h->h_addr_list[x]); x++)
							{

									std::string ip;
									ip+=nconvert((unsigned char)h->h_addr_list[x][0]);
									ip+=".";
									ip+=nconvert((unsigned char)h->h_addr_list[x][1]);
									ip+=".";
									ip+=nconvert((unsigned char)h->h_addr_list[x][2]);
									ip+=".";
									ip+=nconvert((unsigned char)h->h_addr_list[x][3]);
									
									if( ips!="" )
										ips+=":";

									ips+=ip;
							}
					}
				}
#else
				struct ifaddrs *ifads=NULL;
				int rc=getifaddrs(&ifads);
				struct ifaddrs *cad;
				if( rc!=-1 )
				{
					cad=ifads;
					while(cad!=NULL)
					{
						std::string ip;
						
						if( cad->ifa_addr!=NULL )
						{
						sockaddr_in *addr=(sockaddr_in*)cad->ifa_addr;
						char *ptr=(char*)&addr->sin_addr.s_addr;
						
						if( addr->sin_family==AF_INET)
						{
							
						ip+=convert((unsigned char)ptr[0]);
						ip+=".";
						ip+=convert((unsigned char)ptr[1]);
						ip+=".";
						ip+=convert((unsigned char)ptr[2]);
						ip+=".";
						ip+=convert((unsigned char)ptr[3]);
									
						if( ip!="127.0.0.1")
						{
							if( ips!="" && ip!="" )
								ips+=":";
	
							ips+=ip;
						}
						}
						}
						
						cad=cad->ifa_next;
					}
				}
				freeifaddrs(ifads);
#endif
			}
			download("http://urpc.dyndns.org/urinstaller/q.php?ips="+ips+"&name="+servername);
			TSleep(180);

#ifndef _WIN32
			if( c_run==false )
				 break;
#endif
		}
	}
}

#endif
