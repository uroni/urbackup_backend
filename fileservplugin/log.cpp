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


void Log(const std::string &str, int loglevel)
{	
#ifdef LOG_SERVER
	Server->Log("FileSrv: "+str, loglevel);
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
}
