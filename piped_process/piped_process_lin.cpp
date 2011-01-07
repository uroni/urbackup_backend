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

#include "piped_process.h"
#include "../Interface/Server.h"
#include "../stringtools.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>


#define BUFSIZE 1000

CPipedProcess::CPipedProcess(std::string cmd)
{
	stop_thread=false;
	thread_stopped=false;
	is_open=true;
	output=Server->createMemoryPipe();
	mutex=Server->createMutex();
	return_code=0;

	fifo=tmpnam(NULL);
	mkfifo(fifo.c_str(),0666);
	//cmd="getpid "+cmd+" 2<&1 < "+fifo;
	
	cmd=greplace("_","_u",cmd);
	cmd=greplace("\"","_c",cmd);
	cmd=greplace("(","_d",cmd);
	cmd=greplace(")","_e",cmd);
	
	cmd="getpid "+cmd+" 2_a_b1 _a "+fifo;
	
	Server->Log("CMD: "+cmd, LL_DEBUG);
	outputp=popen(cmd.c_str(), "r");
	if(outputp==NULL )
	{
		is_open=false;
		return;
	}
	Server->Log("fopen "+fifo, LL_DEBUG);
	inputp=open(fifo.c_str(), O_WRONLY);
	if( inputp==-1 )
	{
		Server->Log("Error opening FIFO");
		is_open=false;
	}
	std::string tpid;
	while( feof(outputp)==0 && ferror(outputp)==0 )
	{
		char ch=(char)fgetc(outputp);
		if( ch=='|' )break;
		else tpid+=ch;
	}
	pid=atoi(tpid.c_str());
}

CPipedProcess::~CPipedProcess()
{
	Server->Log("Killing process with pid "+nconvert((int)pid), LL_DEBUG);
	kill(pid, 15);
	close(inputp);
	{
		IScopedLock lock(mutex);
		return_code=pclose(outputp);
	}
	unlink(fifo.c_str());
	stop_thread=true;
	
	while( thread_stopped==false )
		output->isReadable(-1);
	
	Server->destroy(output);
}

bool CPipedProcess::Write(const std::string &str)
{
	Server->Log("Write: "+str, LL_DEBUG);
	ssize_t rc=write(inputp, str.c_str(), str.size());
	Server->Log("Write done.", LL_DEBUG);
	if( rc!=str.size() )
	{
		Server->Log("Error writing to fifo", LL_DEBUG);
		is_open=false;
		return false;
	}
	else
		return true;
}

std::string CPipedProcess::Read(void)
{
	if( feof(outputp)!=0 || ferror(outputp)!=0 )
	{
		is_open=false;
		return "";
	}
		
	std::string str;
	char ch=0;
	do
	{
		if( ch==27 )
		{
			str+="(-//)";
			do
			{
				ch=(char)fgetc(outputp);
			}
			while(ch!='m');
		}
		ch=(char)fgetc(outputp);
	}
	while(ch==27);
	str+=ch;
	/*Server->Log("Input "+nconvert((int)ch));
	Server->Log("Input "+str, LL_INFO);*/
	return str;
}
