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

#include "Table.h"

CRATable::~CRATable()
{
	for(size_t i=0;i<tables.size();++i)
	{
		delete tables[i];
	}
}

void CRATable::addObject(std::wstring key, ITable *tab)
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

ITable* CRATable::getObject(std::wstring str)
{
	std::map<std::wstring, ITable*>::iterator iter=table_map.find(str);
	if( iter!= table_map.end() )
	{
		return iter->second;
	}
	else
		return NULL;
}

std::wstring CRATable::getValue()
{
	return L"";
}

size_t CRATable::getSize()
{
	return tables.size();
}

void CRATable::addString(std::wstring key, std::wstring str)
{
	CTablestring *ts=new CTablestring(str);
	this->addObject(key, ts);
}

//-------------------------
CTable::~CTable()
{
	for(std::map<std::wstring, ITable*>::iterator i=table_map.begin();i!=table_map.end();++i)
	{
		delete i->second;
	}
}

void CTable::addObject(std::wstring key, ITable *tab)
{
	table_map[key]=tab;
}

ITable* CTable::getObject(size_t n)
{
	return NULL;
}

ITable* CTable::getObject(std::wstring str)
{
	std::map<std::wstring, ITable*>::iterator iter=table_map.find(str);
	if( iter!= table_map.end() )
	{
		return iter->second;
	}
	else
		return NULL;
}

std::wstring CTable::getValue()
{
	return L"";
}

size_t CTable::getSize()
{
	return table_map.size();
}

void CTable::addString(std::wstring key, std::wstring str)
{
	CTablestring *ts=new CTablestring(str);
	this->addObject(key, ts);
}
//------------------------
CTablestring::CTablestring(std::wstring pStr)
{
	str=pStr;
}

void CTablestring::addObject(std::wstring key, ITable *tab)
{
}

ITable* CTablestring::getObject(size_t n)
{
	return NULL;
}

ITable* CTablestring::getObject(std::wstring key)
{
	return NULL;
}

std::wstring CTablestring::getValue()
{
	return str;
}

size_t CTablestring::getSize()
{
	return 1;
}

void CTablestring::addString(std::wstring key, std::wstring str)
{
}
