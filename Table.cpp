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

#include "Table.h"

CRATable::~CRATable()
{
	for(size_t i=0;i<tables.size();++i)
	{
		delete tables[i];
	}
}

void CRATable::addObject(std::string key, ITable *tab)
{
	table_map[key]=tab;
	tables.push_back(tab);
}

ITable* CRATable::getObject(size_t n)
{
	if( n<tables.size() )
		return tables[n];
	else
		return NULL;
}

ITable* CRATable::getObject(std::string str)
{
	std::map<std::string, ITable*>::iterator iter=table_map.find(str);
	if( iter!= table_map.end() )
	{
		return iter->second;
	}
	else
		return NULL;
}

std::string CRATable::getValue()
{
	return "";
}

size_t CRATable::getSize()
{
	return tables.size();
}

void CRATable::addString(std::string key, std::string str)
{
	CTablestring *ts=new CTablestring(str);
	this->addObject(key, ts);
}

//-------------------------
CTable::~CTable()
{
	for(std::map<std::string, ITable*>::iterator i=table_map.begin();i!=table_map.end();++i)
	{
		delete i->second;
	}
}

void CTable::addObject(std::string key, ITable *tab)
{
	table_map[key]=tab;
}

ITable* CTable::getObject(size_t n)
{
	return NULL;
}

ITable* CTable::getObject(std::string str)
{
	std::map<std::string, ITable*>::iterator iter=table_map.find(str);
	if( iter!= table_map.end() )
	{
		return iter->second;
	}
	else
		return NULL;
}

std::string CTable::getValue()
{
	return "";
}

size_t CTable::getSize()
{
	return table_map.size();
}

void CTable::addString(std::string key, std::string str)
{
	CTablestring *ts=new CTablestring(str);
	this->addObject(key, ts);
}
//------------------------
CTablestring::CTablestring(std::string pStr)
{
	str=pStr;
}

void CTablestring::addObject(std::string key, ITable *tab)
{
}

ITable* CTablestring::getObject(size_t n)
{
	return NULL;
}

ITable* CTablestring::getObject(std::string key)
{
	return NULL;
}

std::string CTablestring::getValue()
{
	return str;
}

size_t CTablestring::getSize()
{
	return 1;
}

void CTablestring::addString(std::string key, std::string str)
{
}
