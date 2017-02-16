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

#include "FileSettingsReader.h"
#include "stringtools.h"
#include "Server.h"
#include <iostream>
#include <memory>
#include "Interface/File.h"

namespace
{
	std::string removeTrailingCR(const std::string& str)
	{
		if (!str.empty() && str[str.size() - 1] == '\r')
			return str.substr(0, str.size() - 1);
		else
			return str;
	}
}

CFileSettingsReader::CFileSettingsReader(std::string pFile)
{
	read(pFile);
}

CFileSettingsReader::~CFileSettingsReader()
{
}

bool CFileSettingsReader::getValue(std::string key, std::string *value)
{
	std::map<std::string,std::string>::iterator i=mSettingsMap.find(key);
	if( i!= mSettingsMap.end() )
	{
		*value=i->second;
		return true;
	}
	return false;
}

std::vector<std::string> CFileSettingsReader::getKeys()
{
	std::vector<std::string> ret;
	for(std::map<std::string,std::string>::iterator i=mSettingsMap.begin();
		i!=mSettingsMap.end();++i)
	{
		ret.push_back(i->first);
	}
	return ret;
}

void CFileSettingsReader::read(const std::string& pFile )
{
	std::auto_ptr<IFile> file(Server->openFile(pFile));
	if (file.get() == NULL)
	{
		return;
	}

	char buf[4096];

	int state = 0;
	std::string key;
	std::string value;

	_u32 read;
	do
	{
		read = file->Read(buf, sizeof(buf));
		for (_u32 i = 0; i < read; ++i)
		{
			char ch = buf[i];
			switch (state)
			{
			case 0:
				if (ch == '#')
				{
					state = 1;
				}
				else if(ch!='\n')
				{
					key += ch;
					state = 2;
				}
				break;
			case 1:
				if (ch == '\n')
				{
					state = 0;
				}
				break;
			case 2:
				if (ch == '\n')
				{
					mSettingsMap[""] = removeTrailingCR(key);
					key.clear();
				}
				else if (ch == '=')
				{
					state = 3;
				}
				else
				{
					key += ch;
				}
				break;
			case 3:
				if (ch == '\n')
				{
					mSettingsMap[key] = removeTrailingCR(value);
					key.clear();
					value.clear();
					state = 0;
				}
				else
				{
					value += ch;
				}
				break;
			}
		}
	} while (read == sizeof(buf));

	if (state == 3)
	{
		mSettingsMap[key] = removeTrailingCR(value);
	}
}
