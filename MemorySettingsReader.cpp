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
		
		mSettingsMap.insert(std::pair<std::wstring,std::wstring>(Server->ConvertToUnicode(key), Server->ConvertToUnicode(value)) );
	}
}

bool CMemorySettingsReader::getValue(std::string key, std::string *value)
{
	std::wstring s_value;
	bool b=getValue( widen(key), &s_value);

	if(b==true)
	{
		std::string nvalue=wnarrow(s_value);
		*value=nvalue;		
		return true;
	}

	return false;
}

bool CMemorySettingsReader::getValue(std::wstring key, std::wstring *value)
{
	std::map<std::wstring,std::wstring>::iterator i=mSettingsMap.find(key);
	if( i!=mSettingsMap.end() )
	{
		*value=i->second;
		return true;
	}
	return false;
	
}
