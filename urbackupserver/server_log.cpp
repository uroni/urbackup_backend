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

#ifndef CLIENT_ONLY

#include "server_log.h"
#include "../stringtools.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "database.h"
#include "../stringtools.h"

std::map<int, std::vector<SLogEntry> > ServerLogger::logdata;
IMutex *ServerLogger::mutex=NULL;

void ServerLogger::Log(int clientid, const std::string &pStr, int LogLevel)
{
	Log(clientid, widen(pStr), LogLevel);
}

void ServerLogger::Log(int clientid, const std::wstring &pStr, int LogLevel)
{
	Server->Log(pStr, LogLevel);

	if(LogLevel<0)
		return;

	if(clientid==0)
		return;

	IScopedLock lock(mutex);

	SLogEntry le;
	le.data=pStr;
	le.loglevel=LogLevel;
	le.time=Server->getTimeSeconds();

	std::map<int, std::vector<SLogEntry> >::iterator iter=logdata.find(clientid);
	if( iter==logdata.end() )
	{
		std::vector<SLogEntry> n;
		n.push_back(le);
		logdata.insert(std::pair<int, std::vector<SLogEntry> >(clientid, n) );
	}
	else
	{
		iter->second.push_back(le);
	}
}

void ServerLogger::init_mutex(void)
{
	mutex=Server->createMutex();
}

void ServerLogger::destroy_mutex(void)
{
	Server->destroy(mutex);
}

std::wstring ServerLogger::getLogdata(int clientid, int &errors, int &warnings, int &infos)
{
	IScopedLock lock(mutex);

	std::wstring ret;

	std::map<int, std::vector<SLogEntry> >::iterator iter=logdata.find(clientid);
	if( iter!=logdata.end() )
	{
		for(size_t i=0;i<iter->second.size();++i)
		{
			SLogEntry &le=iter->second[i];
			
			if(le.loglevel==LL_ERROR)
				++errors;
			else if(le.loglevel==LL_WARNING)
				++warnings;
			else if(le.loglevel==LL_INFO)
				++infos;
			
			ret+=convert(le.loglevel);
			ret+=L"-";
			ret+=convert(le.time);
			ret+=L"-";
			ret+=le.data;
			ret+=L"\n";
		}
		
		return ret;
	}
	else
	{
		return L"";
	}
}

std::wstring ServerLogger::getWarningLevelTextLogdata(int clientid)
{
	IScopedLock lock(mutex);

	std::wstring ret;
	std::map<int, std::vector<SLogEntry> >::iterator iter=logdata.find(clientid);
	if( iter!=logdata.end() )
	{
		for(size_t i=0;i<iter->second.size();++i)
		{
			SLogEntry &le=iter->second[i];
			
			if(le.loglevel>=LL_WARNING)
			{
				if(le.loglevel==LL_WARNING)
					ret+=L"WARNING: ";
				else if(le.loglevel==LL_ERROR)
					ret+=L"ERROR: ";

				ret+=le.data;
				ret+=L"\r\n";
			}
		}
		
		return ret;
	}
	else
	{
		return L"";
	}
}

void ServerLogger::reset(int clientid)
{
	IScopedLock lock(mutex);

	std::map<int, std::vector<SLogEntry> >::iterator iter=logdata.find(clientid);
	if( iter!=logdata.end() )
	{
		iter->second.clear();;
	}
}

#endif //CLIENT_ONLY