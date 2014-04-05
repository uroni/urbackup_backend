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

#include "SettingsReader.h"
#include "stringtools.h"
#ifndef _WIN32
#include <stdlib.h>
#endif

std::string CSettingsReader::getValue(std::string key,std::string def)
{
	std::string value;
	bool b=getValue(key,&value);
	if(b==false)
		return def;
	else
		return value;
}

std::string CSettingsReader::getValue(std::string key)
{
	std::string value;
	bool b=getValue(key,&value);
	if(b==false)
		return "";
	else
		return value;
}

int CSettingsReader::getValue(std::string key, int def)
{
	std::string value;
	bool b=getValue(key,&value);
	if(b==false)
		return def;
	else
		return atoi(value.c_str());
}

float CSettingsReader::getValue(std::string key, float def)
{
	std::string value;
	bool b=getValue(key,&value);
	if(b==false)
		return def;
	else
		return (float)atof(value.c_str());
}

std::wstring CSettingsReader::getValue(std::wstring key,std::wstring def)
{
	std::wstring value;
	bool b=getValue(key,&value);
	if(b==false)
		return def;
	else
		return value;
}

std::wstring CSettingsReader::getValue(std::wstring key)
{
	std::wstring value;
	bool b=getValue(key,&value);
	if(b==false)
		return L"";
	else
		return value;
}

int CSettingsReader::getValue(std::wstring key, int def)
{
	std::wstring value;
	bool b=getValue(key,&value);
	if(b==false)
		return def;
	else
		return atoi(wnarrow(value).c_str());
}

float CSettingsReader::getValue(std::wstring key, float def)
{
	std::wstring value;
	bool b=getValue(key,&value);
	if(b==false)
		return def;
	else
		return (float)atof(wnarrow(value).c_str());
}