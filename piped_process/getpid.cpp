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

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <signal.h>
#include <sys/resource.h>

bool next(const std::string &pData, const size_t & doff, const std::string &pStr)
{
        for(size_t i=0;i<pStr.size();++i)
        {
                if( i+doff>=pData.size() )
                        return false;
                if( pData[doff+i]!=pStr[i] )
                        return false;
        }
        return true;
}

std::string greplace(std::string tor, std::string tin, std::string data)
{
        for(size_t i=0;i<data.size();++i)
        {
                if( next(data, i, tor)==true )
                {
                        data.erase(i, tor.size());
                        data.insert(i,tin);
			i+=tin.size()-1;
                }
        }

        return data;
}

bool first=true;

void termination_handler(int signum)
{
	if( first )
	{
		kill(0,15);
		printf("stop");
		first=false;
	}
}

int main(int argc, char *argv[])
{
	if (signal (SIGINT, termination_handler) == SIG_IGN)
		signal (SIGINT, SIG_IGN);
	if (signal (SIGHUP, termination_handler) == SIG_IGN)
		signal (SIGHUP, SIG_IGN);
	if (signal (SIGTERM, termination_handler) == SIG_IGN)
		signal (SIGTERM, SIG_IGN);

	setpgid(0,0);
	setpriority(PRIO_PROCESS, 0, 10);

	printf("%i|", getpid());
	std::string args;
	for(int i=1;i<argc;++i)
		args+=(std::string)argv[i]+" ";
	args=greplace("_a","<",args);
	args=greplace("_b", "&", args);
	args=greplace("_c", "\"", args);
	args=greplace("_d", "(", args);
	args=greplace("_e", ")", args);
	args=greplace("_u","_", args);
	FILE *p=popen(args.c_str(), "r");
	if( p==NULL )
	{
		printf("ERROR opening command");
		return 0;
	}
	while( feof(p)==0 && ferror(p)==0 )
	{
		char ch[2];
		ch[0]=(char)fgetc(p);
		ch[1]=0;
		printf(ch);
		fflush(stdout);
	}
	/*if( ferror(p)!=0 )
		printf("PIPE error");*/
	return pclose(p);
}