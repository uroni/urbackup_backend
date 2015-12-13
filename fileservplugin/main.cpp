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
#include <fstream>
#include <iostream>
#ifdef _WIN32
#	include <conio.h>
#	include <ws2tcpip.h>
#endif
#include "../Interface/Server.h"
#include "CTCPFileServ.h"
#include "settings.h"
#include "../stringtools.h"
#include "log.h"

std::fstream logfile;
CTCPFileServ *TCPServer=NULL;

#ifdef LINUX
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#endif
#include <algorithm>

#ifdef DLL_EXPORT
#	define EXPORT_METHOD_INT 5
#endif

#ifdef _WIN32
void RestartServer()
{
	_u16 tcpport=TCPServer->getTCPPort();
	_u16 udpport=TCPServer->getUDPPort();
	std::string servername=TCPServer->getServername();
	bool use_fqdn=TCPServer->getUseFQDN();

	TCPServer->KickClients();
	delete TCPServer;
	Sleep(1000);

	TCPServer=new CTCPFileServ;
	int tries=20;
	while(!TCPServer->Start(tcpport, udpport, servername, use_fqdn) )
	{
		Sleep(1000);
		if(tries<=0)
		{
			_exit(32);
		}
		--tries;
	}
}

bool EnumerateInterfaces()
{
	static std::vector<std::vector<char> > ips;
	char hostname[MAX_PATH];

    _i32 rc=gethostname(hostname, MAX_PATH);
     if(rc==SOCKET_ERROR)
		return false;

	 bool new_ifs=false;

	 std::vector<std::vector<char> > new_ips;

	 struct addrinfo* h;
	 if(getaddrinfo(hostname, NULL, NULL, &h)==0)
     {
		 for(addrinfo* ptr = h;ptr!=NULL;ptr=ptr->ai_next)
		 {
			 if(ptr->ai_family==AF_INET || ptr->ai_family==AF_INET6)
			 {
				 std::vector<char> address;
				 address.resize(ptr->ai_addrlen);
				 memcpy(&address[0], ptr->ai_addr, ptr->ai_addrlen);

				 if(std::find(ips.begin(), ips.end(), address)==ips.end())
				 {
					 new_ifs=true;
				 }

				 new_ips.push_back(address);
			 }
		 }

		 freeaddrinfo(h);
     }

	 for(size_t i=0;i<ips.size();++i)
	 {
		 if(std::find(new_ips.begin(), new_ips.end(), ips[i])==new_ips.end())
		 {
			 new_ifs=true;
		 }
	 }

	 ips=new_ips;

	 return new_ifs;
}

void LookForInterfaceChanges()
{
	static int num=-1;
	if(num>=10 )
	{
		num=0;
		if(EnumerateInterfaces()==true)
		{
			RestartServer();
		}
	}
	if( num==-1 )
		EnumerateInterfaces();
	++num;
}

bool SetPrivilege(
    HANDLE hToken,          // access token handle
    LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
    BOOL bEnablePrivilege   // to enable or disable privilege
    ) 
{
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if ( !LookupPrivilegeValue( 
            NULL,            // lookup privilege on local system
            lpszPrivilege,   // privilege to lookup 
            &luid ) )        // receives LUID of privilege
    {
        Log("LookupPrivilegeValue error: "+convert((int)GetLastError()), LL_ERROR ); 
        return false; 
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    // Enable the privilege or disable all privileges.

    if ( !AdjustTokenPrivileges(
           hToken, 
           FALSE, 
           &tp, 
           sizeof(TOKEN_PRIVILEGES), 
           (PTOKEN_PRIVILEGES) NULL, 
           (PDWORD) NULL) )
    { 
          Log("AdjustTokenPrivileges error: "+convert((int)GetLastError()), LL_ERROR ); 
          return false; 
    } 

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)

    {
          Log("The token does not have the specified privilege.", LL_ERROR);
          return false;
    } 

    return true;
}

HRESULT ModifyPrivilege(
    IN LPCTSTR szPrivilege,
    IN BOOL fEnable)
{
    HRESULT hr = S_OK;
    TOKEN_PRIVILEGES NewState;
    LUID             luid;
    HANDLE hToken    = NULL;

    // Open the process token for this process.
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken ))
    {
        Log("Failed OpenProcessToken", LL_ERROR);
        return ERROR_FUNCTION_FAILED;
    }

    // Get the local unique ID for the privilege.
    if ( !LookupPrivilegeValue( NULL,
                                szPrivilege,
                                &luid ))
    {
        CloseHandle( hToken );
        Log("Failed LookupPrivilegeValue", LL_ERROR);
        return ERROR_FUNCTION_FAILED;
    }

    // Assign values to the TOKEN_PRIVILEGE structure.
    NewState.PrivilegeCount = 1;
    NewState.Privileges[0].Luid = luid;
    NewState.Privileges[0].Attributes = 
              (fEnable ? SE_PRIVILEGE_ENABLED : 0);

    // Adjust the token privilege.
    if (!AdjustTokenPrivileges(hToken,
                               FALSE,
                               &NewState,
                               0,
                               NULL,
                               NULL))
    {
        Log("Failed AdjustTokenPrivileges", LL_ERROR);
        hr = ERROR_FUNCTION_FAILED;
    }

    // Close the handle.
    CloseHandle(hToken);

    return hr;
}
#endif //_WIN32

#ifdef LINUX_DAEMON
bool c_run=true;
void termination_handler(int signum)
{
	c_run=false;
	_exit(2);
}
#endif

#ifdef CONSOLE_ON
int main(int argc, char* argv[])
{
#elif AS_SERVICE
void my_init_fcn(void)
{
#elif EXPORT_METHOD_INT
int start_server_int(unsigned short tcpport, unsigned short udpport, const std::string &pSname, const bool *pDostop, bool use_fqdn)
{
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
#endif

#ifdef _DEBUG
	{
#endif
#ifdef LINUX_DAEMON
		std::cout << "Starting as Daemon..." << std::endl;
		if( fork()==0 )
		{
			setsid();
			for (int i=getdtablesize();i>=0;--i) close(i);
			int i=open("/dev/null",O_RDWR);
			dup(i);
			dup(i);
		}
		else
			exit(0);


		if (signal (SIGINT, termination_handler) == SIG_IGN)
			signal (SIGINT, SIG_IGN);
		if (signal (SIGHUP, termination_handler) == SIG_IGN)
			signal (SIGHUP, SIG_IGN);
		if (signal (SIGTERM, termination_handler) == SIG_IGN)
			signal (SIGTERM, SIG_IGN);
	
		c_run=true;
#endif

#ifndef _DEBUG
#ifndef AS_SERVICE
#ifndef EXPORT_METHOD_INT
	WriteReg();
#endif
#endif
#endif

	std::string servername;

#ifdef CONSOLE_ON
	if(argc==2)
		servername=argv[1];
#endif

#ifdef BACKUP_SEM
#ifdef _WIN32
	HRESULT hr=ModifyPrivilege(SE_BACKUP_NAME, TRUE);
	if(!SUCCEEDED(hr))
	{
		Log("Failed to modify backup privileges", LL_ERROR);
	}
	else
	{
		Log("Backup privileges set successfully", LL_DEBUG);
	}
	hr=ModifyPrivilege(SE_SECURITY_NAME, TRUE);
	if(!SUCCEEDED(hr))
	{
		Log("Failed to modify backup privileges (SE_SECURITY_NAME)", LL_ERROR);
	}
	else
	{
		Log("Backup privileges set successfully (SE_SECURITY_NAME)", LL_DEBUG);
	}
	hr=ModifyPrivilege(SE_RESTORE_NAME, TRUE);
	if(!SUCCEEDED(hr))
	{
		Log("Failed to modify backup privileges (SE_RESTORE_NAME)", LL_ERROR);
	}
	else
	{
		Log("Backup privileges set successfully (SE_RESTORE_NAME)", LL_DEBUG);
	}
#endif
#endif

#ifdef LOG_FILE
#ifdef _WIN32
	logfile.open("C:\\urinstallsrv.log", std::ios::out | std::ios::binary );
#else
	logfile.open("/var/log/urinstallsrv.log", std::ios::out | std::ios::binary );
#endif
#endif

	/*std::fstream out(testfilename.c_str(), std::ios::out | std::ios::binary );
	if( out.is_open()==false )
		Log("Can't create testfile... Could perhaps cause problems.");


	char* buffer=new char[1024];
	for(size_t i=0;i<1024;++i)
		out.write(buffer, 1024);
	out.close();
	delete[] buffer;
*/

#ifdef _WIN32
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

	TCPServer=new CTCPFileServ;

#ifndef EXPORT_METHOD_INT
	bool suc=TCPServer->Start(55634,55635,servername);
#else
	bool suc=TCPServer->Start(tcpport, udpport, (pSname=="")?servername:pSname, use_fqdn);
#endif

	if(suc==false)
	{
		delete TCPServer;
		TCPServer=NULL;
		return 99;
	}


#ifndef AS_SERVICE
	while(true)
	{
#if defined(_DEBUG) && defined(_WIN32)
		if( _kbhit() )
		{
			break;
		}
#endif
		bool b=TCPServer->Run();
		
		if( b==false )
			break;
		
#ifdef LINUX_DEAMON
		if( c_run==false )
			break;
#endif		
#if EXPORT_METHOD_INT
		if(*pDostop==true)
		{
			TCPServer->KickClients();
			break;
		}
#endif
	}

	delete TCPServer;
#endif //AS_SERVICE

#ifndef AS_SERVICE
	return 2;
#endif
#ifdef _DEBUG
	}
#endif
}



