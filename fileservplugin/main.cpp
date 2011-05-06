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
#include <fstream>
#include <iostream>
#ifdef _WIN32
#	include <conio.h>
#endif
#include "../Interface/Server.h"
#include "CTCPFileServ.h"
#include "settings.h"
#include "../stringtools.h"
#include "CampusThread.h"
#include "log.h"

std::fstream logfile;
#ifdef CAMPUS
CCampusThread *ct=NULL;
#endif
CTCPFileServ *TCPServer=NULL;

#ifdef _DEBUG
#ifdef _WIN32
#pragma comment(lib, "vld.lib")
#endif
#endif

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


#ifdef DLL_EXPORT
#	define EXPORT_METHOD_INT 5
#endif


void WriteReg(void)
{
#ifndef LINUX
	HKEY hkey; 
	LONG ret=RegCreateKeyExA(HKEY_LOCAL_MACHINE,"SOFTWARE\\UrInstaller\\settings",0,0,REG_OPTION_NON_VOLATILE,KEY_QUERY_VALUE,NULL,&hkey,0);

	bool write=false;
	if( ret!=ERROR_SUCCESS)
		write=true;
	else
	{
		DWORD dwRet=-1;
		DWORD dwSize=sizeof(DWORD);
		LONG ret=RegQueryValueExA(hkey,"start_urinstallsrv",NULL, NULL, (LPBYTE)&dwRet, &dwSize ); 

		if( ret!=ERROR_SUCCESS || dwRet==1 )
			write=true;
		else
			write=false;
	}

	if( write==true)
	{
		HKEY hkey; 
		RegCreateKeyExA(HKEY_LOCAL_MACHINE,"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",0,0,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,NULL,&hkey,0);

		char cString[MAX_PATH]; 
		DWORD size=GetModuleFileNameA(0,cString,MAX_PATH);
		RegSetValueExA(hkey,"UrInstallSrv",0,REG_SZ,(BYTE*)cString,size);
		RegCloseKey(hkey);
	}
#endif
}

#ifdef _WIN32
void RestartServer()
{
	_u16 tcpport=TCPServer->getTCPPort();
	_u16 udpport=TCPServer->getUDPPort();
	std::string servername=TCPServer->getServername();

	TCPServer->KickClients();
	delete TCPServer;
	Sleep(120000);

	TCPServer=new CTCPFileServ;
	int tries=20;
	while(!TCPServer->Start(tcpport,udpport,servername) )
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
	static std::vector<_u32> ips;
	char hostname[MAX_PATH];
    struct    hostent* h;
    _u32     address;

    _i32 rc=gethostname(hostname, MAX_PATH);
     if(rc==SOCKET_ERROR)
		return false;

	 bool new_ifs=false;

	 std::vector<_u32> new_ips;

	 if(NULL != (h = gethostbyname(hostname)))
     {
		for(_u32 x = 0; (h->h_addr_list[x]); x++)
        {
               
			((char*)(&address))[0] = h->h_addr_list[x][0];
			((char*)(&address))[1] = h->h_addr_list[x][1];
            ((char*)(&address))[2] = h->h_addr_list[x][2];
            ((char*)(&address))[3] = h->h_addr_list[x][3];
			
			bool found=false;
			for(size_t i=0;i<ips.size();++i)
			{
				if(ips[i]==address )
				{
					found=true;
					break;
				}
			}

			if( found==false )
			{
				new_ifs=true;
			}

			new_ips.push_back(address);
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
        Log("FileSrv: LookupPrivilegeValue error: %i", (int)GetLastError() ); 
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
          Log("FileSrv: AdjustTokenPrivileges error: %i", (int)GetLastError() ); 
          return false; 
    } 

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)

    {
          Log("FileSrv: The token does not have the specified privilege.");
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
        Log("Failed OpenProcessToken");
        return ERROR_FUNCTION_FAILED;
    }

    // Get the local unique ID for the privilege.
    if ( !LookupPrivilegeValue( NULL,
                                szPrivilege,
                                &luid ))
    {
        CloseHandle( hToken );
        Log("Failed LookupPrivilegeValue");
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
        Log("Failed AdjustTokenPrivileges");
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

std::string getServerName(void);

#ifdef CONSOLE_ON
int main(int argc, char* argv[])
{
#elif AS_SERVICE
void my_init_fcn(void)
{
#elif EXPORT_METHOD_INT
int start_server_int(unsigned short tcpport, unsigned short udpport, const std::string &pSname, const bool *pDostop)
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
		Log("Failed to modify backup privileges");
	}
	else
	{
		Log("Backup privileges set successfully");
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
	bool suc=TCPServer->Start(tcpport, udpport, (pSname=="")?servername:pSname);
#endif

	if(suc==false)
	{
		delete TCPServer;
		TCPServer=NULL;
		return 99;
	}

#ifdef CAMPUS
	ct=new CCampusThread(getServerName());
	Server->createThread(ct);
#endif
	
#ifndef AS_SERVICE
	while(true)
	{
#ifdef _DEBUG
		if( _kbhit() )
		{
			break;
		}
#endif
		bool b=TCPServer->Run();
		
		if( b==false )
			break;

#ifdef _WIN32
		LookForInterfaceChanges();
#endif
		
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
#ifdef CAMPUS
	delete ct;
#endif
	return 2;
#endif
#ifdef _DEBUG
	}
#endif
}



