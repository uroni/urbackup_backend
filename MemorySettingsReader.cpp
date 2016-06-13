/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2016 Martin Raiber
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

#include "MemorySettingsReader.h"
#include "Server.h"
#include "stringtools.h"

CMemorySettingsReader::CMemorySettingsReader(const std::string &pData)
{
	int num_lines=linecount(pData);
	for(int i=0;i<num_lines;++i)
	{
		std::string line=getline(i,pData);

		if(line.size()<2 || line[0]=='#' )
			continue;

		std::string key=getuntil("=",line);
		std::string value;

		if(key=="")
			value=line;
		else
		{
			line.erase(0,key.size()+1);
			value=line;
		}		
		
		mSettingsMap.insert(std::pair<std::string,std::string>((key), (value)) );
	}
}

bool CMemorySettingsReader::getValue(std::string key, std::string *value)
{
	std::map<std::string,std::string>::iterator i=mSettingsMap.find(key);
	if( i!=mSettingsMap.end() )
	{
		*value=i->second;
		return true;
	}
	return false;
	
}

std::vector<std::string> CMemorySettingsReader::getKeys()
{
	std::vector<std::string> ret;
	for(std::map<std::string,std::string>::iterator i=mSettingsMap.begin();i!=mSettingsMap.end();++i)
	{
		ret.push_back(i->first);
	}
	return ret;
}
