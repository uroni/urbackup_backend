/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2014 Martin Raiber
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

#include "server_log.h"
#include "../stringtools.h"
#include "../Interface/Database.h"
#include "../Interface/Query.h"
#include "database.h"
#include "../stringtools.h"
#include <limits.h>

std::map<logid_t, std::vector<SLogEntry> > ServerLogger::logdata;
IMutex *ServerLogger::mutex=NULL;
std::map<int, SCircularData> ServerLogger::circular_logdata;
logid_t ServerLogger::logid_gen;
std::map<logid_t, int> ServerLogger::logid_client;

const size_t circular_logdata_buffersize=20;

void ServerLogger::Log(logid_t logid, const std::string &pStr, int LogLevel)
{
	Server->Log(pStr, LogLevel);

	IScopedLock lock(mutex);

	logCircular(logid_client[logid], pStr, LogLevel);

	if(LogLevel<0)
		return;

	logMemory(logid, pStr, LogLevel);
}

void ServerLogger::Log(logid_t logid, const std::wstring &pStr, int LogLevel)
{
	Server->Log(pStr, LogLevel);

	IScopedLock lock(mutex);

	const std::string utf8Str=Server->ConvertToUTF8(pStr);

	logCircular(logid_client[logid], utf8Str, LogLevel);

	if(LogLevel<0)
		return;

	logMemory(logid, utf8Str, LogLevel);
}

void ServerLogger::logMemory(logid_t logid, const std::string &pStr, int LogLevel)
{
	SLogEntry le;
	le.data=pStr;
	le.loglevel=LogLevel;
	le.time=Server->getTimeSeconds();

	std::map<logid_t, std::vector<SLogEntry> >::iterator iter=logdata.find(logid);
	if( iter==logdata.end() )
	{
		std::vector<SLogEntry> n;
		n.push_back(le);
		logdata.insert(std::pair<logid_t, std::vector<SLogEntry> >(logid, n) );
	}
	else
	{
		iter->second.push_back(le);
	}
}

void ServerLogger::logCircular(int clientid, const std::string &pStr, int LogLevel)
{
	std::map<int, SCircularData>::iterator iter=circular_logdata.find(clientid);
	SCircularData *data;
	if(iter==circular_logdata.end())
	{
		data=&circular_logdata[clientid];
		data->data.resize(circular_logdata_buffersize);
		data->idx=0;
		data->id=0;
	}
	else
	{
		data=&iter->second;
	}

	SCircularLogEntry& entry=data->data[data->idx];
	entry.id=data->id++;
	entry.loglevel=LogLevel;
	entry.time=Server->getTimeSeconds();
	entry.utf8_msg=pStr;

	data->idx=(data->idx+1)%circular_logdata_buffersize;
}

void ServerLogger::init_mutex(void)
{
	mutex=Server->createMutex();
}

void ServerLogger::destroy_mutex(void)
{
	Server->destroy(mutex);
}

std::wstring ServerLogger::getLogdata(logid_t logid, int &errors, int &warnings, int &infos)
{
	IScopedLock lock(mutex);

	std::wstring ret;

	std::map<logid_t, std::vector<SLogEntry> >::iterator iter=logdata.find(logid);
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
			ret+=Server->ConvertToUnicode(le.data);
			ret+=L"\n";
		}
		
		return ret;
	}
	else
	{
		return L"";
	}
}

std::string ServerLogger::getWarningLevelTextLogdata(logid_t logid)
{
	IScopedLock lock(mutex);

	std::string ret;
	std::map<logid_t, std::vector<SLogEntry> >::iterator iter=logdata.find(logid);
	if( iter!=logdata.end() )
	{
		for(size_t i=0;i<iter->second.size();++i)
		{
			SLogEntry &le=iter->second[i];
			
			if(le.loglevel>=LL_WARNING)
			{
				if(le.loglevel==LL_WARNING)
					ret+="WARNING: ";
				else if(le.loglevel==LL_ERROR)
					ret+="ERROR: ";

				ret+=le.data;
				ret+="\r\n";
			}
		}
		
		return ret;
	}
	else
	{
		return "";
	}
}

void ServerLogger::reset(logid_t id)
{
	IScopedLock lock(mutex);

	std::map<logid_t, std::vector<SLogEntry> >::iterator iter=logdata.find(id);
	if( iter!=logdata.end() )
	{
		iter->second.clear();
	}
}

std::vector<SCircularLogEntry> ServerLogger::getCircularLogdata( int clientid, size_t minid )
{
	IScopedLock lock(mutex);

	std::map<int, SCircularData>::const_iterator iter=circular_logdata.find(clientid);
	if(iter!=circular_logdata.end())
	{
		if(minid==std::string::npos)
			return iter->second.data;

		for(size_t i=0;i<iter->second.data.size();++i)
		{
			if(iter->second.data[i].id>minid &&
				iter->second.data[i].id!=std::string::npos)
				return iter->second.data;
		}
		return std::vector<SCircularLogEntry>();
	}
	else
	{
		return std::vector<SCircularLogEntry>();
	}
}

logid_t ServerLogger::getLogId( int clientid )
{
	IScopedLock lock(mutex);

	logid_t ret= std::make_pair(logid_gen.first++, 0);

	logid_client[ret] = clientid;

	return ret;
}

