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

#include "Interface/Types.h"
#include "Interface/Database.h"

#include "SettingsReader.h"
#include "stringtools.h"
#include "Server.h"

#include "DBSettingsReader.h"

#include <iostream>

CDBSettingsReader::CDBSettingsReader(THREAD_ID tid, DATABASE_ID did, const std::string &pTable, const std::string &pSQL)
{
	table=pTable;
	db=Server->getDatabase(tid, did);
	if(pSQL.empty() )
		query=db->Prepare("SELECT value FROM "+table+" WHERE key=?", false);
	else
		query=db->Prepare(pSQL, false);
}

CDBSettingsReader::CDBSettingsReader(IDatabase *pDB, const std::string &pTable, const std::string &pSQL)
	: db(pDB)
{
	table=pTable;
	if(pSQL.empty() )
		query=pDB->Prepare("SELECT value FROM "+table+" WHERE key=?", false);
	else
		query=pDB->Prepare(pSQL, false);
}

CDBSettingsReader::~CDBSettingsReader()
{
	db->destroyQuery(query);
}

bool CDBSettingsReader::getValue(std::string key, std::string *value)
{
	if(query==NULL)
	{
		return false;
	}

	query->Bind(key);
	db_results res=query->Read();
	query->Reset();

	if( res.size()>0 )
	{
		*value=res[0]["value"];
		return true;
	}
	else
		return false;
}

std::vector<std::string> CDBSettingsReader::getKeys()
{
	return std::vector<std::string>();
}

CDBMemSettingsReader::CDBMemSettingsReader(THREAD_ID tid, DATABASE_ID did, const std::string & pTable, const std::string & pSQL)
{
	init(Server->getDatabase(tid, did), pTable, pSQL);
}

CDBMemSettingsReader::CDBMemSettingsReader(IDatabase * pDB, const std::string & pTable, const std::string & pSQL)
{
	init(pDB, pTable, pSQL);
}

bool CDBMemSettingsReader::getValue(std::string key, std::string * value)
{
	str_map::iterator it = table.find(key);
	if (it != table.end())
	{
		*value = it->second;
		return true;
	}
	else
	{
		return false;
	}
}

std::vector<std::string> CDBMemSettingsReader::getKeys()
{
	std::vector<std::string> ret;
	for (str_map::iterator it = table.begin(); it != table.end(); ++it)
	{
		ret.push_back(it->first);
	}
	return ret;
}

void CDBMemSettingsReader::init(IDatabase * pDB, const std::string & pTable, const std::string & pSQL)
{
	db_results res;
	if (pSQL.empty())
		res = pDB->Read("SELECT key, value FROM " + pTable + " WHERE key=?");
	else
		res = pDB->Read(pSQL);

	for (size_t i = 0; i < res.size(); ++i)
	{
		table.insert(std::make_pair(res[i]["key"], res[i]["value"]));
	}
}