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

//---------
#include "../vld.h"
#include <stdarg.h>
#include <string>
#include <iostream>
#include "CriticalSection.h"
#include "../stringtools.h"
#include "settings.h"
#include "../Interface/Server.h"

#ifndef _DEBUG
#include <fstream>
extern std::fstream logfile;
#endif

CriticalSection logcs;

#ifdef _DEBUG
#	define LOG_ON
#	undef LOG_OFF
#endif

#ifdef _RELEASE_CONSOLE
#	define LOG_ON
#	undef LOG_OFF
#endif

#ifdef LOG_OFF
#undef LOG_ON
#endif


void Log(const char *pStr,...)
{
#ifndef LOG_OFF
	va_list args;
	va_start( args, pStr );
	std::string tmpstr=pStr;
	for(size_t i=0;i<tmpstr.size();++i)
	{
		if( i+1<tmpstr.size() && tmpstr[i]=='%')
		{
			std::string rep;
			bool tr=false;
			if(tmpstr[i+1]=='s' )
			{
				rep=va_arg( args, char*);
				tr=true;
			}
			else if(tmpstr[i+1]=='f')
			{
				rep=nconvert((float)(va_arg( args, double)) );
				tr=true;
			}
			else if(tmpstr[i+1]=='d')
			{
				rep=nconvert((float)(va_arg( args, double)) );
				tr=true;
			}
			else if(tmpstr[i+1]=='i')
			{
				rep=nconvert((int)(va_arg( args, int)) );
				tr=true;
			}
			else if(tmpstr[i+1]=='x')
			{
				rep=nconvert((int)(va_arg( args, int)) );
				tr=true;
			}
			if(tr)
			{
				tmpstr.erase(i, 2);
				tmpstr.insert(i, rep);
				i+=rep.size();
			}
		}
	}
	
#ifdef LOG_SERVER
	Server->Log(tmpstr, LL_DEBUG);
#endif

#ifdef LOG_CONSOLE

	logcs.Enter();
	std::cout << tmpstr << std::endl;
	logcs.Leave();
#endif
#ifdef LOG_FILE 
	logcs.Enter();
	logfile << tmpstr << "\r\n";
	logfile.flush();
	logcs.Leave();
#endif
#endif
}
